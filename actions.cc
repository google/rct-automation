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
#include <sys/types.h>
#include <sys/stat.h>
#include "dirent.h"

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include <gflags/gflags.h>
#include "db.h"
#include "automationstate.h"
#include "playlist.h"
#include "requirementengine.h"
#include "mplayersession.h"
#include "stdio.h"
#include <string>
#include <glog/logging.h>
#include "registerable-inl.h"
#include <boost/tokenizer.hpp>
#include <boost/bind.hpp>

DEFINE_string(legalid, "legalid", "Name of playlist that contains legal IDs.");
DEFINE_int32(legalid_max_length, 60, "Maximium length of legal ID to consider playing.");

namespace automator {

class ScheduleCommand : public RequirementEngine::Registrar {
  virtual void handle_command(const time_t &deadline, const automation::Requirement& command) = 0;
  RequirementEngine::Registrar::callback get_callback() {
    return boost::bind<void>(&ScheduleCommand::handle_command, this, _1, _2);
  }
};

typedef boost::tokenizer<boost::char_separator<char> > tokenizer;

class DoNothingCommand : public ScheduleCommand {
 public:
  const std::string get_command() { return "NO_OP"; }
  void handle_command(const time_t &deadline, const automation::Requirement &command) {
    return;
  }
};
REGISTER_COMMAND(DoNothingCommand);

class PlayFilesCommand : public ScheduleCommand {
 public:
  const std::string get_command() { return "PLAY_FILES"; }
  void handle_command(const time_t &deadline, const automation::Requirement& req) {
    AutomationState *as = AutomationState::get_state();
    MplayerSession& player = *CHECK_NOTNULL(as->get_player());

    sqlite3 *db = DatabaseOpen();
    PlayableItem item(db);

    for (RepeatedPtrField<automation::PlayableItem>::const_iterator it = req.playlist().items().begin();
         it != req.playlist().items().end();
         ++it) {
      if (it->has_playableitemid()) {
        item.Fetch(it->playableitemid());
        player.Play(item);
      } else {
        player.Play(*it);
      }
    }
    sqlite3_close(db);
  }
};
REGISTER_COMMAND(PlayFilesCommand);

class LegalIDCommand : public ScheduleCommand {
 public:
  const std::string get_command() { return "LEGAL_ID"; }
  void handle_command(const time_t &deadline, const automation::Requirement &command) {
    AutomationState *as = AutomationState::get_state();
    LOG(INFO) << "Playing ID";
    sqlite3 *db = DatabaseOpen();
    Playlist legalid(db);
    Playlist::LockByName(db, FLAGS_legalid);
    CHECK(legalid.FetchShuffled(FLAGS_legalid));
    PlayableItem item(db);
    do {
      if (legalid.Size() <= 0) {
        LOG(FATAL) << "UNABLE TO PLAY LEGAL ID ";
      }
      legalid.PopWithTimelimit(FLAGS_legalid_max_length, &item);
    } while (!as->get_player()->Play(item));
    sqlite3_close(db);
  }
};
REGISTER_COMMAND(LegalIDCommand);

class SetPlaylistCommand : public ScheduleCommand {
 public:
  const std::string get_command() { return "SET_MAINSHOW"; }
  void handle_command(const time_t &deadline, const automation::Requirement &msg) {
    AutomationState *as = AutomationState::get_state();
    if (msg.has_target_playlistname()) {
      as->SetMainshow(msg.target_playlistname());
    } else {
      as->SetMainshow();
    }
  }
};
REGISTER_COMMAND(SetPlaylistCommand);

} // namespace automator
