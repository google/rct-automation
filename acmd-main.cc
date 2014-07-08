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
#include <boost/bind.hpp>
#include <fstream>
#include <glog/logging.h>
#include <gflags/gflags.h>
#include <iostream>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "db.h"
#include "base.h"
#include "automationstate.h"
#include "http.h"
#include "mplayersession.h"
#include "playableitem.h"
#include "playlist.h"
#include "requirementengine.h"
#include "playlist.pb.h"
#include "protostore.h"

DEFINE_string(bumpers, "unused", "bumpers - this is unused in this binary needed as a linking hack");
DEFINE_string(command, "list", "Command to run - list, load, replace, append, dump, setup");
DEFINE_string(playlist, "default-playlist", "Target playlist");
DEFINE_int32(weight, -1, "used with command=setup to set the weight");

int shutdown_requested;
 
int main(int argc, char **argv) {
  google::InitGoogleLogging(argv[0]);
  google::SetUsageMessage("Usage");
  google::ParseCommandLineFlags(&argc, &argv, true);
  google::InstallFailureSignalHandler();
  std::srand(time(NULL));

  DatabaseHandle db(DatabaseOpen());

  PlaylistPtr null;
  AutomationState automation(db, NULL);
  MplayerSession mp;
  Playlist candidate(db);
  candidate.Fetch(FLAGS_playlist);
  if (FLAGS_command == "list") {
    printf("%s", Playlist::FetchAllLists(db).DebugString().c_str());
  } else if (FLAGS_command == "load") {
    char buf[512];
    while (fgets(buf, sizeof(buf), stdin)) {
      if (buf[strlen(buf)-1] == '\n') {
        buf[strlen(buf)-1] = '\0';
      }
      if (!strlen(buf)) {
        continue;
      }
      PlayableItem item(db);
      bool found = item.fetch(buf);
      VLOG(30) << "found state " << found << " duration " << item.data().duration();
      if (!found && item.data().duration() > 0) {
        VLOG(5) << "Attempting to store";
        item.Replace();
      } else if (item.data().duration() <= 0) {
        continue;
      }

      printf("%ld\t%s\n", item.data().playableitemid(), buf);
    }
  } else if (FLAGS_command == "replace" || FLAGS_command == "append") {
    if (FLAGS_command == "replace") {
      candidate.mutable_data().clear_playableitemid();
    }
    char buf[1024];
    while (fgets(buf, sizeof(buf), stdin)) {
      char *p = buf;
      while (!isspace(*p) && *p != '\0') {
        ++p;
      }
      *p = '\0';
      int itemid = atoi(buf);
      candidate.mutable_data().add_playableitemid(itemid);
    } 
    candidate.Replace();
  } else if (FLAGS_command == "dump") {
    printf("%s",candidate.data().DebugString().c_str());
  } else if (FLAGS_command == "setup") {
    if (FLAGS_weight >= 0) {
      candidate.mutable_data().set_weight(FLAGS_weight);
    } 
    candidate.Replace();
  }
  google::protobuf::ShutdownProtobufLibrary();
  sqlite3_close(db); 
  sqlite3_shutdown();
}
