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

#include "automationstate.h"
#include "playlist.h"
#include <glog/logging.h>
#include <stdio.h>
#include <unistd.h>
#include <gflags/gflags.h>
#include "requirementengine.h"
#include "mplayersession.h"

DEFINE_bool(defaulthuman, false, "If true, when automation starts a human is in control.");
DEFINE_int32(bumpercutoff, 200, "If we have <= bumpercutoff seconds remaining after we have "
  "exhausted our options with mainshow and override playlists, use the bumpers playlist "
  "for passing time.  Otherwise, a new mainshow will be selected.");
DEFINE_int32(sleepcutoff, 4, "If we have <= sleepcutoff seconds remaining after we have "
  "exhausted our options with mainshow, override, and bumperlist [assuming sleepcutoff < bumpercutoff]"
  " playlists, we can sleep for the remainder of time.  This value => max amount of dead air "
  "we'll intentionally generate.");

DECLARE_string(bumpers);

AutomationState *AutomationState::state_;
__thread MplayerSession *AutomationState::player_;

AutomationState::AutomationState(sqlite3* db, MplayerSession* player) :
  db_(db),
  re_(boost::shared_ptr<RequirementEngine>(new RequirementEngine(db))),
  main_player_(player),
  override_(FLAGS_defaulthuman),
  override_playlist_(new Playlist(db)),
  mainshow_(new Playlist(db)),
  bumperlist_(new Playlist(db)) {

  player_ = main_player_;
  bumperlist_->NeverSave();
  mainshow_->NeverSave();
  override_playlist_->NeverSave();

  SetMainshow();
  state_ = this;
}

bool AutomationState::RunOnce() {
  time_t deadline, gap;

  // Attempt to yield to a human
  if (ManualOverride()) {
    // If we did anything in manual override, skip any requirements that happened
    // before we returned.
    re_->set_time(time(NULL));
  }

  if (bumperlist_->Size() == 0) {
    ResetBumpers();
  } else {
    VLOG(5) << "Bumperlist of size " << bumperlist_->Size();
  }

  automation::Schedule next_requirements;
  re_->FillNext(&next_requirements, &deadline, &gap);
  VLOG(10) << "Deadline set to " << deadline << "after which we play " << next_requirements.DebugString();

  if (time(NULL) >= deadline) {
    re_->RunBlock(deadline, &next_requirements);
    // We're doing this needlessly most of the time.  We only need to do this if we
    // played bumpers...
    ResetBumpers();
    return true;
  }

  PlayableItem next_track(db_);
  GetMainshow()->PopWithTimelimit(deadline - time(NULL) + gap, &next_track);

  MplayerSession &mp = *CHECK_NOTNULL(get_player());
  if (next_track.data().has_filename()) {
    // We found something in our mainshow_ that fits in the alloted time; play it.
    mp.Play(next_track);
    return true;
  } else {
    // OK, we weren't able to find something to play in our mainshow_.

    PlayableItem next_bumper(db_);

    if((deadline - time(NULL)) >= FLAGS_bumpercutoff && !GetMainshow()->Size()) {
      // We have more than 200 seconds left before our requirement is due,
      // or the mainshow_ is empty.  Instead of falling back to bumpers,
      // let's just get a new mainshow_.
      LOG(ERROR) << "Abandoning mainshow (" << mainshow_->Name() << ") due to too much remaining time.";
      SetMainshow();
      return false;
    } else {
      bumperlist_->PopWithTimelimit(deadline - time(NULL) + gap, &next_bumper);
      if (next_bumper.data().has_filename()) {
        // We found a bumper to play.  Play it.
        mp.Play(next_bumper);
        return true;
      }

      // We have no bumpers left.  Let's check one time to see if we still
      // have time to kill...
      int time_left = deadline - time(NULL);
      if(time_left <= 0) {
        return true;
      }
      // Well, shoot, we do have time to kill.  If it's under sleepcutoff,
      // sleep it off
      if(time_left <= FLAGS_sleepcutoff) {
        sleep(time_left);
        return true; // we "played" silence, so return true here
      } else {
        LOG(ERROR) << "Too much time left to sleep post-bumpers.";
        return false;
      }
    }
  }
  CHECK(false); // Not reached
  return false;
}
extern int shutdown_requested;

bool AutomationState::ManualOverride() {
  bool did_anything = false;
  PlaylistPtr op = AutomationState::get_state()->get_override_playlist();

  while (AutomationState::get_state()->get_manual_override() || op->Size()) {
    did_anything = true;
    PlayableItem next(db_);
    op->PopFront(&next);
    if (next.data().has_filename()) {
      AutomationState::get_state()->get_player()->Play(next);
    } else {
      // The value of override_ might have changed by now as we didn't hold
      // a lock on this, which means we'll sleep (and possibly return
      // true here, although typically we'd be doing that anyway if in this
      // codepath) perhaps incorrectly here.  I don't think this is a concern.
      usleep(500);
      if (shutdown_requested) {
        exit(0);
      }
    }
  }
  return did_anything;
}

void AutomationState::SetMainshow() {
  mainshow_->Fetch();
  LOG(INFO) << "Randomly selected playlist \"" << mainshow_->Name() << "\" as mainshow.";
  return;
}
void AutomationState::SetMainshow(std::string playlist) {
  if (playlist.empty()) {
    return SetMainshow();
  }
  if (mainshow_->FetchShuffled(playlist)) {
    LOG(INFO) << "Selected \"" << mainshow_->Name() << "\" as mainshow, per request.";
  } else {
    LOG(WARNING) << "Requested playlist \"" << playlist << "\" not found.";
    SetMainshow();
  }
}
PlaylistPtr AutomationState::GetMainshow() {
  return mainshow_;
}
void AutomationState::ResetBumpers() {
  VLOG(5) << "Reloading bumpers";
  if (FLAGS_bumpers.empty()) {
    bumperlist_->FetchSuperlist(LLONG_MAX, 0);
  } else { 
    Playlist::LockByName(db_, FLAGS_bumpers);
    bumperlist_->Fetch(FLAGS_bumpers);
  }
} 
