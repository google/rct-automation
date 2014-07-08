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
#ifndef PLAYLIST_H
#define PLAYLIST_H

#include <string>
#include "sqlite3.h"
#include "base.h"
#include "playableitem.h"
#include "playableitem.pb.h"
#include "playlist.pb.h"
#include "protostore.h"

class Playlist;

typedef boost::shared_ptr<Playlist> PlaylistPtr;

class Playlist : public automation::ThreadSafeProto<automation::Playlist> {
 public:
  static void LockByName(sqlite3 *db, const std::string &target);
  static automation::Playlists FetchAllLists(sqlite3 *db);
  void PopWithTimelimit(int seconds, PlayableItem *target); 
  void PopFront(PlayableItem *target);

  int Size() const;
  std::string Name() const;
  void ApplyMergeRequest(const automation::PlaylistMergeRequest& request, bool replace);

  int get_weight() const;

  automation::Playlist Filter(const std::string& pattern) const;
  bool Fetch();
  bool FetchShuffled(const std::string& playlistname);
  bool Fetch(const std::string& playlistname);
  bool FetchSuperlist(long long limit, long long offset);
  bool Fetch(int playlistID);

  Playlist(sqlite3 *db);
 private:
  bool CompareDurations(sqlite3_int64 item1, sqlite3_int64 item2, PlayableItem* fetcher);
  typedef google::protobuf::RepeatedField< ::google::protobuf::int64> list_type;
  int size_locked() const;

  DISALLOW_COPY_AND_ASSIGN(Playlist);
};

#endif
