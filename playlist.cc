/*
 *   Copyright 2012-2014 Google, Inc.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */
#include <algorithm>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <set>
#include <sstream>
#include <glog/logging.h>
#include <stdint.h>

#include "automationstate.h"
#include "playableitem.h"
#include "playlist.h"
#include "playlist.pb.h"
#include "protostore.h"

#include <boost/bind.hpp>

#define INT64_MAX LLONG_MAX

using automation::ProtoStore;

automation::Playlists Playlist::FetchAllLists(sqlite3 *db) {
  automation::ProtoStore<automation::Playlist> pstore(db, "Playlists_with_size");

  vector<automation::Playlist> playlists;
  pstore.LoadAll(&playlists, INT64_MAX, 0);

  automation::Playlists list;

  for (vector<automation::Playlist>::iterator it = playlists.begin(); it != playlists.end(); ++it) {
    automation::Playlist *next = list.add_item();
    next->CopyFrom(*it);
  }
  return list; 
}

Playlist::Playlist(sqlite3 *db) :
  automation::ThreadSafeProto<automation::Playlist>(db) {
}
void Playlist::LockByName(sqlite3 *db, const std::string &name) {
  LOG(INFO) << "Locking playlist " << name;
  automation::ThreadSafeProto<automation::PlaylistLock> lock(db);
  lock.mutable_data().set_name(name);
  try {
    lock.Replace();
  } catch (std::exception& e) {
    LOG(FATAL) << "Unable to lock mandatory playlist: " << name << " - does it exist?";
  }
}

void Playlist::PopWithTimelimit(int seconds, PlayableItem *result) {
  boost::mutex::scoped_lock lock(mutex_);
  RepeatedField<int64>* songlist = canonical_.mutable_playableitemid();
  LOG(INFO) << "In playlist " << canonical_.name() << " for " << seconds << " of time with up to "
            << size_locked() << " choices";

  for (list_type::iterator it = songlist->begin(); it != songlist->end(); ++it) {
    if (*it == 0) { continue; }
    result->Fetch(*it);
    if (result->data().playableitemid() && result->data().duration() <= seconds) {
      *it = 0;
      return;
    }
  }
  result->Clear();
  LOG(WARNING) << "No acceptable item found.";
  return;
}
void Playlist::PopFront(PlayableItem *result) {
  boost::mutex::scoped_lock lock(mutex_);
  RepeatedField<int64>* songlist = canonical_.mutable_playableitemid();
  PlayableItem item(db_);

  for (list_type::iterator it = songlist->begin(); it != songlist->end(); ++it) {
    if (*it == 0) { continue; }
    result->Fetch(*it);
    *it = 0;
    return;
  }
  result->Clear();
  return;
}

void Playlist::ApplyMergeRequest(const automation::PlaylistMergeRequest& request, bool replace) {
  // This is presumably not the most elegant way of getting the protobuf code to ignore 
  // types, but it does work.
  automation::Playlist merger;
  merger.ParseFromString(request.SerializeAsString()); 
  if (replace) {
    canonical_.clear_items();
    canonical_.clear_playableitemid();
  }
  canonical_.MergeFrom(merger);
}

automation::Playlist Playlist::Filter(const std::string& regexp) const {
  automation::Playlist result;

#ifdef USE_RE2
  RE2 re("(?i)"+regexp);

  if (!re.ok()) {
    return result;
  }
#else
  regex_t re;
  if (regcomp(&re, regexp.c_str(), REG_ICASE | REG_EXTENDED | REG_NOSUB)) {
    return result;
  }
#endif

  boost::mutex::scoped_lock lock(mutex_);
  const RepeatedField<int64>& songlist = canonical_.playableitemid();
  PlayableItem item(db_);

  for (list_type::const_iterator it = songlist.begin(); it != songlist.end(); ++it) {
    item.Fetch(*it);
    if (item.matches(re)) {
      automation::PlayableItem *newitem = result.add_items();
      newitem->CopyFrom(item.data());
      result.add_playableitemid(newitem->playableitemid());
    }
  }
  return result;
}

bool Playlist::Fetch() {
  SetTable("Playlists_random_weight");
  boost::mutex::scoped_lock lock(mutex_);

  canonical_.Clear();
  bool result = Load(&canonical_);
  SetTable("Playlists");
  return result;
} 

bool Playlist::Fetch(const std::string& playlistname) {
  SetTable("Playlists_with_children");

  boost::mutex::scoped_lock lock(mutex_);
  canonical_.Clear();
  canonical_.set_name(playlistname);
  bool result = Load(&canonical_);
  SetTable("Playlists");
  return result;
}
bool Playlist::FetchShuffled(const std::string& playlistname) {
  SetTable("Playlists_with_shuffled_children");

  boost::mutex::scoped_lock lock(mutex_);
  canonical_.Clear();
  canonical_.set_name(playlistname);
  bool result = Load(&canonical_);
  SetTable("Playlists");
  return result;
}
bool Playlist::FetchSuperlist(long long limit, long long offset) {
  char buf[1024];

  snprintf(buf, sizeof buf, "CREATE TEMPORARY VIEW Playlists_with_everything AS "
" SELECT 0 AS PlaylistID, 'ALL TRACKS' AS name, 0 as weight, "
"        group_concat(PlayableItemID) AS PlayableItemID "
"        FROM (SELECT PlayableItemID FROM PlayableItem ORDER BY duration DESC LIMIT %lld OFFSET %lld) group by 1;", limit, offset);

  
  CHECK(sqlite3_exec(db_, buf, NULL, NULL, NULL) == SQLITE_OK) << sqlite3_errmsg(db_);

  SetTable("Playlists_with_everything");

  boost::mutex::scoped_lock lock(mutex_);
  canonical_.Clear();
  bool result = LoadById(&canonical_, 0);
  CHECK(sqlite3_exec(db_, "DROP VIEW Playlists_with_everything", NULL, NULL, NULL) == SQLITE_OK) << sqlite3_errmsg(db_);

  SetTable("Playlists");
  return result;
}

bool Playlist::Fetch(int playlistID) {
  boost::mutex::scoped_lock lock(mutex_);
  canonical_.Clear();
  SetTable("Playlists_with_children");

  bool result = LoadById(&canonical_, playlistID);
  SetTable("Playlists");
  return result;
}

int Playlist::Size() const {
  boost::mutex::scoped_lock lock(mutex_);
  return size_locked();
}
std::string Playlist::Name() const {
  boost::mutex::scoped_lock lock(mutex_);
  return canonical_.name();
}
int Playlist::size_locked() const {
  const RepeatedField<int64>& songlist = canonical_.playableitemid();
  int size = 0;
  for (list_type::const_iterator it = songlist.begin(); it != songlist.end(); ++it) {
    if (*it) {
      size++;
    } 
  } 
  return size;
}

