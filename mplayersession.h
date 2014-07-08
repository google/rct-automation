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

#ifndef MPLAYER_SESSION_H
#define MPLAYER_SESSION_H

#include <vector>
#include <string>
#include "base.h"
#include <boost/thread/mutex.hpp>
#include "playerstate.pb.h"
#include "playableitem.h"

class MplayerSession {
 public:
  MplayerSession();

  // Two versions of play - the one that takes the PlayableItem reference, and
  // another that takes the raw proto.  The raw proto version doesn't increment
  // playcount (or otherwise touch the database)
  bool Play(PlayableItem &item);
  bool Play(const automation::PlayableItem &item);

  void Pause();
  void Unpause();
  void Stop();
  void SetSpeed(double speed);
  void Seek(double timepos);
  
  void MergeState(automation::PlayerState *dest);

 private:
  bool is_timedout();

  std::string GetProperty(std::string property);
  DISALLOW_COPY_AND_ASSIGN(MplayerSession);

  // mutex_ guards everything except state_.  It should be held
  // by anything that's interacting with the child mplayer process.
  boost::mutex mutex_;
  FILE *slave_pipe_;
  FILE *mplayer_stdout_;
  int mplayer_fd_;
  int errorfd_;
  int mplayer_pid_;
  int last_alive_;

  // state_mutex_ guards the automation::PlayerState that contains information
  // about our current state.
  boost::mutex state_mutex_;
  automation::PlayerState state_;

};

#endif
