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
#ifndef REQUIREMENT_ENGINE_HEADER_H
#define REQUIREMENT_ENGINE_HEADER_H

#include "base.h"
#include <sqlite3.h>
#include <boost/thread/mutex.hpp>
#include <boost/function.hpp>
#include "requirement.pb.h"
#include "registerable-inl.h"

typedef boost::function<void(time_t deadline, const automation::Requirement& config)> radio_callback;

class RequirementEngine {
 public:
  void FillNext(automation::Schedule* next, time_t *deadline, time_t *gap);
  
  void set_time(const time_t &time) { internal_time_ = time; }
  void HandleReboot();
  void CopyTo(automation::Schedule *output);
  void CopyFrom(const automation::Schedule& input);
  void Save();
  static void CheckValidity();
  void RunBlock(time_t deadline, const automation::Schedule*);

  RequirementEngine(sqlite3 *db);
  REGISTER_REGISTRAR(RequirementEngine, radio_callback);
 private:
  bool IsDue(const automation::Requirement& item, time_t candidate_time);

  // Compute the effective schedule off of the stored and implicit
  automation::Schedule EffectiveSchedule();

  DISALLOW_COPY_AND_ASSIGN(RequirementEngine);
  sqlite3 *db_;

  boost::mutex mutex_;
  automation::Schedule schedule_;
  time_t internal_time_;
};

#endif
