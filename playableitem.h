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
#ifndef PLAYABLE_ITEM_H
#define PLAYABLE_ITEM_H 1

#include <string>
#include "sqlite3.h"
#include <boost/shared_ptr.hpp>
#include "playlist.pb.h"
#include "playableitem.pb.h"
#include "protostore.h"
#include "base.h"
#ifdef USE_RE2
#include <re2/re2.h>
#else
#include <sys/types.h>
#include <regex.h>
#endif

class PlayableItem;
typedef boost::shared_ptr<PlayableItem> PlayableItemPtr;

class PlayableItem : public automation::ThreadSafeProto<automation::PlayableItem> {
 public:
  bool fetch(const std::string& filename);

#ifdef USE_RE2
  bool matches(const RE2& pattern);
#else
  bool matches(const regex_t& pattern);
#endif
  void IncrementPlaycount();
  PlayableItem(sqlite3 *db);
 private:
  int CalculateDuration();
  DISALLOW_COPY_AND_ASSIGN(PlayableItem);
};

#endif
