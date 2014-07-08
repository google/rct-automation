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

#ifndef _MESSAGESTORE_H
#define _MESSAGESTORE_H

#include "sqlite3.h"
#include <string>
#include <google/protobuf/dynamic_message.h>

using namespace google::protobuf;

namespace automation {

class MessageStore {
 public:
  MessageStore(sqlite3 *db, const Descriptor *desc, const std::string& tablename);
  bool Load(Message* lookup);
  bool LoadById(Message* lookup, int64_t id);
  int Insert(Message* value);
  int Replace(Message* value);
  int Update(Message* value);
  void NeverSave();
  ~MessageStore();

 protected:
  void SetTable(const std::string& tablename);
  int InsertOrReplace(Message* value, std::string cmd);
  int BindFromFields(const Message& object, sqlite3_stmt *ps);
  bool ProtoFromRows(sqlite3_stmt *ps, Message *result);

  sqlite3 *db_;
 private:
  const Descriptor *desc_;
  DynamicMessageFactory factory_;
  sqlite3_stmt *lookup_by_id_;

 protected:
  std::string table_;
  bool never_save_;
};

}
#endif
