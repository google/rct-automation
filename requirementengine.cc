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
#include "requirementengine.h"
#include "mplayersession.h"
#include <string>
#include <glog/logging.h>
#include <gflags/gflags.h>

#include "playlist.pb.h"
#include "requirement.pb.h"
#include "protostore.h"

DEFINE_bool(implicit_legalid, false, "If true, implicitly run a legal ID at the top of the hour.");
DEFINE_int32(implicit_legalid_gap, 180, "Gap for implicit legal ID requirement.");

RequirementEngine::RequirementEngine(sqlite3 *db) :
  db_(db), 
  internal_time_(time(NULL)) {

  automation::BasicProtoStore pstore(db);
  pstore.Load<automation::Schedule>(&schedule_);
}
automation::Schedule RequirementEngine::EffectiveSchedule() {
  if (FLAGS_implicit_legalid) {
    automation::Schedule effective;
    automation::Requirement *req = effective.add_schedule();
    req->set_type(automation::Requirement::LEGAL_ID);
    req->mutable_when()->add_constrained_seconds(0);
    req->mutable_when()->add_constrained_minutes(0);
    req->mutable_when()->set_gap(FLAGS_implicit_legalid_gap);
    req->set_reboot(true);
    VLOG(3) << effective.DebugString();
    effective.MergeFrom(schedule_);
    return effective;
  } else {
    return schedule_;
  }
}

void RequirementEngine::Save() {
  automation::BasicProtoStore pstore(db_);

  boost::mutex::scoped_lock lock(mutex_);
  pstore.Save<automation::Schedule>(&schedule_);
}
void RequirementEngine::CheckValidity() {
  automation::Requirement req;
  const EnumDescriptor *e = req.GetDescriptor()->FindEnumTypeByName("Command");
  CHECK(e != NULL) << "Unable to find Command on Requirement proto definition.";
  RequirementEngine::Registrar::CallbackMap &cm = RequirementEngine::Registrar::get_callbackmap();
  for (int i = 0; i < e->value_count() ; ++i) {
    if (!cm.count(e->value(i)->name())) {
      LOG(FATAL) << "Unknown command " << e->value(i)->name();
    }
  }
}

void RequirementEngine::HandleReboot() {
  boost::mutex::scoped_lock lock(mutex_);
  automation::Schedule effective = EffectiveSchedule();
  RepeatedPtrField<automation::Requirement>* reqlist = effective.mutable_schedule();
  automation::Schedule reboot_commands;
  VLOG(1) << "automation comes alive!";
  for (RepeatedPtrField<automation::Requirement>::iterator it = reqlist->begin();
         it != reqlist->end();
         ++it) {
    if (it->reboot()) {
      automation::Requirement *p = reboot_commands.add_schedule();
      p->CopyFrom(*it);
      p->set_internal_time_advance(-1);
    }
  }
  RunBlock((time_t)0, &reboot_commands);
}
void RequirementEngine::CopyTo(automation::Schedule *output) {
  boost::mutex::scoped_lock lock(mutex_);
  output->CopyFrom(schedule_);
}
void RequirementEngine::CopyFrom(const automation::Schedule &input) {
  boost::mutex::scoped_lock lock(mutex_);
  schedule_.CopyFrom(input);
}
void RequirementEngine::FillNext(automation::Schedule* next, time_t* deadline, time_t* gap) {
  boost::mutex::scoped_lock lock(mutex_);

  // We set a default deadline of an hour from now, just in case nothing is scheduled.
  *deadline = time(NULL)+3600;
  // We will end up getting a gap from a requirement here, but let's start with an
  // impossibly large gap here, so we can safely do *gap = min(*gap, item-gap) later.
  *gap = 86400 * 365 * 20;

  const automation::Schedule effective = EffectiveSchedule();
 
  for(int target_time = internal_time_ ; target_time < internal_time_ + 86400*7 && !next->schedule_size() ; ++target_time) {
    const RepeatedPtrField<automation::Requirement>& reqlist = effective.schedule();
    for (RepeatedPtrField<automation::Requirement>::const_iterator it = reqlist.begin();
           it != reqlist.end();
           ++it) {
      if (IsDue(*it, target_time)) {
        *deadline = target_time;
        *gap = min<time_t>(*gap, it->when().gap());
        automation::Requirement *next_item = next->add_schedule();
        next_item->CopyFrom(*it);
      }
    }
  }
  return;
}
void RequirementEngine::RunBlock(time_t deadline, const automation::Schedule* next) {
    RequirementEngine::Registrar::CallbackMap &cm = RequirementEngine::Registrar::get_callbackmap();
    int internal_time_advance = 1;
 
    for (RepeatedPtrField<automation::Requirement>::const_iterator it = next->schedule().begin();
           it != next->schedule().end();
           ++it) {
      if (it->internal_time_advance() < 0 && internal_time_advance > 0) {
        internal_time_advance = -1;
      } else {
        internal_time_advance = max<int64>(internal_time_advance, it->internal_time_advance());
      }
      std::string command_identifier = automation::Requirement::Command_descriptor()->FindValueByNumber(it->type())->name();
      if (cm.count(command_identifier)) {
        cm[command_identifier](deadline, *it);
      } else {
        LOG(ERROR) << "Unknown comand " << command_identifier;
      }
    }
    if (internal_time_advance < 0) {
      VLOG(5) << "Setting internal time to now";
      internal_time_ = time(NULL);
    } else {
      VLOG(5) << "Incrementing internal time by " << internal_time_advance << " seconds";
      internal_time_ += internal_time_advance;
    }
}
bool RequirementEngine::IsDue(const automation::Requirement& item, time_t candidate_time) {
  struct tm time_spec;
  localtime_r(&candidate_time, &time_spec); 

  const automation::TimeSpecification time = item.when();
  if (time.only_at_times_size()) {
    for (RepeatedField<int64>::const_iterator it = time.only_at_times().begin(); it != time.only_at_times().end(); ++it) {
      if (*it == candidate_time) {
        return true;
      }
    }
    return false;
  }
  if (time.constrained_dom_size()) {
    bool constraint_met = false;
    for (RepeatedField<int64>::const_iterator it = time.constrained_dom().begin(); it != time.constrained_dom().end(); ++it) {
      if (*it == time_spec.tm_mday) {
        constraint_met = true;
        break;
      }
    }
    if (!constraint_met) return false;
  }
  if (time.constrained_dow_size()) {
    bool constraint_met = false;
    for (RepeatedField<int64>::const_iterator it = time.constrained_dow().begin(); it != time.constrained_dow().end(); ++it) {
      if (*it == time_spec.tm_wday) {
        constraint_met = true;
        break;
      }
    }
    if (!constraint_met) return false;
  }
  if (time.constrained_hours_size()) {
    bool constraint_met = false;
    for (RepeatedField<int64>::const_iterator it = time.constrained_hours().begin(); it != time.constrained_hours().end(); ++it) {
      if (*it == time_spec.tm_hour) {
        constraint_met = true;
        break;
      }
    }
    if (!constraint_met) return false;
  }
  if (time.constrained_minutes_size()) {
    bool constraint_met = false;
    for (RepeatedField<int64>::const_iterator it = time.constrained_minutes().begin(); it != time.constrained_minutes().end(); ++it) {
      if (*it == time_spec.tm_min) {
        constraint_met = true;
        break;
      }
    }
    if (!constraint_met) return false;
  }
  if (time.constrained_seconds_size()) {
    bool constraint_met = false;
    for (RepeatedField<int64>::const_iterator it = time.constrained_seconds().begin(); it != time.constrained_seconds().end(); ++it) {
      if (*it == time_spec.tm_sec) {
        constraint_met = true;
        break;
      }
    }
    if (!constraint_met) return false;
  }
  return true;
}

