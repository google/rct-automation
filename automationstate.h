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

#ifndef AUTOMATIONSTATE_H
#define AUTOMATIONSTATE_H
#include "sqlite3.h"
#include "base.h"
#include "playlist.h"
#include "mplayersession.h"
#include <string>
#include <boost/shared_ptr.hpp>

class RequirementEngine;

// AutomationState is a singleton class that holds pointers to the
// current state of the automation system.  It is typically used
// by code running in different subsystems that are attempting to
// query or modify the state of the system.  It is thread safe.
class AutomationState {
 public:
  // Constructor for the AutomationState object.  The AutomationState class is provided with a copy
  // of the open sqlite3 database associated with the current instance, a reference to
  // the RequirementEngine that dictates the schedule of events (Requirements) for us.  It should already
  // be configured at this point.  It receives a reference to the MplayerSession object it is to use,
  // a reference to the PlaylistCollection it should use.
  AutomationState(sqlite3 *db, MplayerSession *mp);

  // Advance the running automation state, by possibly playing a track (and blocking until that track
  // has finished playing)
  bool RunOnce();

  // Returns a reference to the mplayersession object
  MplayerSession *get_player() {
    if(player_ == NULL) {
      VLOG(5) << "Creating new mplayer for thread";
      player_ = new MplayerSession;
    }
    return player_;
  }

  MplayerSession *get_mainplayer() {
    return main_player_;
  }

  // Accessors for override_, which controls whether we are operating in manual
  // override mode.  In manual override more, we only play tracks that are provided
  // to us.  This enables a user of the Web API to use the automation system
  // not as automation, but as a server to deliver digital tracks on-demand in an
  // interactive fashion.
  void set_manual_override(bool value) { override_ = value; } 
  bool get_manual_override() { return override_; } 

  static AutomationState *get_state() { return AutomationState::state_; };
  boost::shared_ptr<RequirementEngine> get_requirement_engine() const { return re_; }
  PlaylistPtr get_override_playlist() { return override_playlist_; }
  PlaylistPtr get_bumperlist() { return bumperlist_; }

  void SetMainshow(std::string args);
  void SetMainshow();
  PlaylistPtr GetMainshow();
 private:
  void ResetBumpers();
  DISALLOW_COPY_AND_ASSIGN(AutomationState);
  bool ManualOverride();

  static AutomationState* state_;
  sqlite3* const db_;
  boost::shared_ptr<RequirementEngine> const re_;

  static __thread MplayerSession* player_;
  MplayerSession* main_player_;

  bool override_;
  PlaylistPtr const override_playlist_;
  PlaylistPtr const mainshow_;
  PlaylistPtr const bumperlist_;
};
 

#endif

