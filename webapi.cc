/*
 *   Copyright 2013 Google, Inc.
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
#include <exception>
#include <gflags/gflags.h>
#include <glog/logging.h>  
#include "http.h"
#include "mplayersession.h"
#include <ostream>
#include <pion/PionAlgorithms.hpp>
#include "playableitem.h"
#include "playlist.h"
#include "requirementengine.h"

#include "db.h"
#include "playlist.pb.h"
#include "requirement.pb.h"
#include "sql.pb.h"
#include "json_protobuf.h"

#include "protostore.h"

DEFINE_bool(expose_sql, true, "If false, disable the /sql webapi endpoint.");

DECLARE_string(legalid);
std::string WebAPI::apikey;

bool WebAPI::ReadFromDatabase(sqlite3 *db) {
  WebAPI::apikey = "hello this i sa test";

  return true;
}

class OverrideCommand : public WebCommand {
  const std::string get_command() { return "/override"; }
  void handle_command(HTTPRequestPtr& request, HTTPResponseWriterPtr writer, const std::string& remote_user) {
    AutomationState *as = AutomationState::get_state();
    if (request->getResource() == "/override/enable") {
      writer->write("Override enabled\n");
      as->set_manual_override(true);
    } else if (request->getResource() == "/override/disable") {
      as->get_mainplayer()->Unpause();
      as->get_mainplayer()->SetSpeed(1.0);
      writer->write("Override disabled\n");
      as->set_manual_override(false);
    }
  }
};
REGISTER_COMMAND(OverrideCommand);
class RequirementsCommand : public WebCommand {
  const std::string get_command() { return "/requirements"; }
  void handle_command(HTTPRequestPtr& request, HTTPResponseWriterPtr writer, const std::string& remote_user) {
    AutomationState *as = AutomationState::get_state();
    DatabaseHandle db(DatabaseOpen());
    if (request->getResource() == "/requirements/fetch") {
      automation::Schedule output;
      as->get_requirement_engine()->CopyTo(&output);
      ReturnMessage(output);
    } else if(request->getResource() == "/requirements/update") {
      automation::Schedule update_request = LoadMessage<automation::Schedule>();
      VLOG(5) << "Updating with schedule " << update_request.DebugString();
      as->get_requirement_engine()->CopyFrom(update_request);
      as->get_requirement_engine()->Save();
    } else if(request->getResource() == "/requirements/runonce") {
      RequirementEngine re_isolated(db);
      automation::Schedule run_now;
      run_now.add_schedule()->CopyFrom(LoadMessage<automation::Requirement>());
      LOG(INFO) << remote_user << " requests command " << run_now.DebugString();
      re_isolated.RunBlock(0, &run_now);
    }
  }
};
REGISTER_COMMAND(RequirementsCommand);
class SQLResult {
 private:

 public:
  static int AddRow(void * arg, int ncol, char **fields, char **columns) {
    automation::SQLResult *data = (automation::SQLResult *)arg;
    automation::SQLRow row;
    if (!data->has_column()) {
      for(int i = 0; i < ncol; ++i) {
        row.add_data(columns[i]);
      }
      data->mutable_column()->CopyFrom(row);
    }
    row.Clear();
    for(int i = 0; i < ncol; ++i) {
      if (fields[i]) {
        row.add_data(fields[i]);
      } else {
        row.add_data("");
      }
    }
    data->add_row()->CopyFrom(row);
    return 0;
  }

};

class SQLCommand : public WebCommand {
  const std::string get_command() { return "/sql"; }
  void handle_command(HTTPRequestPtr& request, HTTPResponseWriterPtr writer, const std::string& remote_user) {
    if (!FLAGS_expose_sql) {
      return;
    }
    const char *cmd = request_->getContent();
    char *errmsg;
    DatabaseHandle db(DatabaseOpen());
    automation::SQLResult result;
    LOG(INFO) << "SQL API: " << cmd;
    if (sqlite3_exec(db, cmd, &SQLResult::AddRow, &result, &errmsg) != SQLITE_OK) {
      sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
    }
    if (errmsg) {
      writer << errmsg;
      sqlite3_free(errmsg);
      return;
    }
    ReturnMessage(result);
  }
};
REGISTER_COMMAND(SQLCommand);

class PlaylistCommand : public WebCommand {
  const std::string get_command() { return "/playlist"; }
  void handle_command(HTTPRequestPtr& request, HTTPResponseWriterPtr writer, const std::string& remote_user) {
    using automation::ProtoStore;
    HTTPTypes::QueryParams& params = request->getQueryParams(); 
    DatabaseHandle db(DatabaseOpen());
    ProtoStore<automation::Playlist> pstore(db);

    PlaylistPtr ptr;

    if (request->getResource().find("/playlist/fetch") != std::string::npos) {
      Playlist lookup(db);
      if ((ptr = FetchPlaylistFromParams(db)) && ptr.get()) {
        if (params_.count("alsosave")) {
          automation::Playlist temp = Filter(ptr.get());
          temp.clear_playlistid();
          PlaylistPtr newlist = GetNewlist(db);
          VLOG(5) << "Newlist prepared" << newlist->data().DebugString();
          newlist->MergeFrom(temp);
          VLOG(5) << "About to save " << newlist->data().DebugString();
          newlist->Replace();
          ReturnMessage(newlist->data());
        } else {
          FilterAndReturn(ptr.get());
        }
      } else {
        LOG(INFO) << "Nope " << lookup.data().DebugString();
      }
    } else if (request->getResource().find("/playlist/all") != std::string::npos) {
      ReturnMessage(Playlist::FetchAllLists(db));
    } else if (request->getResource().find("/playlist/update") != std::string::npos) {
      automation::PlaylistMergeRequest update_request = LoadMessage<automation::PlaylistMergeRequest>();
      bool overwrite = false;
      if (params.count("overwrite")) {
        overwrite = true;
      }
      PlaylistPtr ptr = FetchPlaylistFromParams(db);
      if (!ptr.get() && update_request.has_playlistid()) {
        ptr.reset(new Playlist(db));
        ptr->Fetch(update_request.playlistid());
      }
      // At this point, we've either fetched the playlist off of the id in the request, or we've 
      // fetched a special list, and shouldn't attempt to save it back.
      update_request.clear_playlistid();
      if (ptr.get()) {
        VLOG(5) << "applying merge request";
        ptr->ApplyMergeRequest(update_request, overwrite);
        VLOG(5) << "replacing now";
        ptr->Replace();
        automation::Playlist output;
        ptr->CopyTo(&output);
        ReturnMessage(output);
      } else {
        writer << "Invalid request.";
      }
    } else {
      LOG(WARNING) << "Unknown resource " << request->getResource();
    }
  }
  PlaylistPtr GetNewlist(sqlite3 *db) {
    char buf[128];
    int newnum = 0;
    PlaylistPtr newlist(new Playlist(db));
    do {
      snprintf(buf, sizeof(buf), "New Playlist %d", ++newnum);
      VLOG(20) << "About to lookup" << newlist->data().DebugString();
    } while (newlist->Fetch(buf));
    return newlist;
  }

  PlaylistPtr FetchPlaylistFromParams(sqlite3 *db) {
    if (params_.count("fetchall")) {
      PlaylistPtr lookup(new Playlist(db));
      int64_t limit = ArgumentOrDefault<int64_t>("limit", LLONG_MAX);
      int64_t offset = ArgumentOrDefault<int64_t>("offset", 0);
      lookup->FetchSuperlist(limit, offset);
      return lookup; 
    }
    if (params_.count("mainshow")) {
      return AutomationState::get_state()->GetMainshow();
    }
    if (params_.count("override")) {
      return AutomationState::get_state()->get_override_playlist();
    }
    if (params_.count("bumperlist")) {
      return AutomationState::get_state()->get_bumperlist();
    }
    if (params_.count("new")) {
      return GetNewlist(db);
    }
    if (!params_.count("id")) {
      return PlaylistPtr();
    }
    sqlite3_int64 id = atoll(params_.equal_range("id").first->second.c_str());

    PlaylistPtr copy(new Playlist(db));
    if (copy->Fetch(id)) {
      return copy;
    }

    return PlaylistPtr();
  }
  automation::Playlist Filter(Playlist *input) {
    automation::Playlist output;
    if (params_.count("filter")) {
      output = input->Filter(params_.equal_range("filter").first->second);
      if (params_.count("noitems") || params_.count("alsosave")) {
        output.clear_items();
      } else {
        output.clear_playableitemid();
      }
    } else {
      input->CopyTo(&output);
      output.clear_items();
    }
    return output;
  }
  void FilterAndReturn(Playlist* input) {
    automation::Playlist output = Filter(input);
    while(output.items_size() > ArgumentOrDefault<int64_t>("truncate", LLONG_MAX)) {
      output.mutable_items()->RemoveLast();
    }

    ReturnMessage(output);
  }
  

};
REGISTER_COMMAND(PlaylistCommand);
      
class PlayerCommand : public WebCommand {
  const std::string get_command() { return "/player"; }
  void handle_command(HTTPRequestPtr& request, HTTPResponseWriterPtr writer, const std::string& remote_user) {
    AutomationState *as = AutomationState::get_state();
    if (request->getResource() == "/player/pause" && as->get_manual_override()) {
      as->get_mainplayer()->Pause();
    } else if (request->getResource() == "/player/stop") {
      as->get_mainplayer()->Stop();
    } else if (request->getResource() == "/player/state") {
      automation::PlayerState ps;
      as->get_mainplayer()->MergeState(&ps);
      ReturnMessage(ps);
    } else if (request->getResource() == "/player/speed") {
      double speed = ArgumentOrDefault<double>("speed", 1.0);
      as->get_mainplayer()->SetSpeed(speed);
    } else if (request->getResource() == "/player/seek") {
      double timepos = ArgumentOrDefault<double>("seek", 0.0);
      as->get_mainplayer()->Seek(timepos);
    }
  }
 public:
};
REGISTER_COMMAND(PlayerCommand);

