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

#include <stdio.h>
#include <google/protobuf/descriptor.h>
#include <glog/logging.h>
#include "sqlite3.h"
#include "boost/algorithm/string/join.hpp"
#include "boost/algorithm/string/split.hpp"
#include "boost/algorithm/string/classification.hpp"
#include <boost/thread/mutex.hpp>
#include <string>
#include <vector>
#include <google/protobuf/dynamic_message.h>
#include "messagestore.h"
#include <exception>

using std::vector;
using std::string;
using namespace google::protobuf;

namespace automation {

class constraintexception_decl : public std::exception {
  virtual const char* what() const throw() {
    return "Invalid request: DB constraint not satisfied.";
  }
} ConstraintException;
 

MessageStore::MessageStore(sqlite3 *db, const Descriptor *desc, const std::string& table) : db_(db), desc_(desc), lookup_by_id_(NULL), table_(table), never_save_(false) {
}
void MessageStore::NeverSave() {
  never_save_ = true;
}

void MessageStore::SetTable(const std::string& table) {
  if (lookup_by_id_ && table_ != table) {
    sqlite3_finalize(lookup_by_id_);
    lookup_by_id_ = NULL;
  }
  table_ = table;
}
MessageStore::~MessageStore() {
  if (lookup_by_id_) {
    sqlite3_finalize(lookup_by_id_);
    lookup_by_id_ = NULL;
  }
}
bool MessageStore::LoadById(Message* lookup, int64_t id) {
  if (lookup_by_id_ == NULL) {
    std::string query = "SELECT * from " + table_ + " WHERE " + desc_->FindFieldByNumber(1)->name() + " = ?";
    CHECK(SQLITE_OK == sqlite3_prepare_v2(CHECK_NOTNULL(db_), query.c_str(), -1, &lookup_by_id_, NULL)) << sqlite3_errmsg(db_);
  }

  sqlite3_bind_int64(lookup_by_id_, 1, id);
  bool rval = ProtoFromRows(lookup_by_id_, lookup);
  sqlite3_reset(lookup_by_id_);
  return rval;
}

bool MessageStore::Load(Message* lookup) {
  std::string query = "SELECT * from " + table_ + " WHERE ";

  const Reflection* reflection = lookup->GetReflection();
  vector<const FieldDescriptor *> fields;
  reflection->ListFields(*lookup, &fields);
  for (vector<const FieldDescriptor *>::iterator it = fields.begin() ; it != fields.end(); ++it) {
    const FieldDescriptor* fd = *it;
    CHECK(!fd->is_repeated()) << "Lookups on repeated fields disallowed";
    query += fd->name() + " = ?";
  }
  if (fields.size() == 0) {
    query += "1";
  }
  
  sqlite3_stmt *ps;
  VLOG(75) << "about to query" << query;
  CHECK(SQLITE_OK == sqlite3_prepare_v2(CHECK_NOTNULL(db_), query.c_str(), -1, &ps, NULL)) << sqlite3_errmsg(db_);
  BindFromFields(*lookup, ps);  
  bool result = ProtoFromRows(ps, lookup);
  VLOG(80) << "returning " << lookup->DebugString() << " from load with retval " << result;
  CHECK(SQLITE_OK == sqlite3_reset(ps));
  CHECK(SQLITE_OK == sqlite3_finalize(ps));
  return result;
}
int MessageStore::Insert(Message* value) {
  return InsertOrReplace(value, "INSERT");
}
int MessageStore::Replace(Message* value) {
  return InsertOrReplace(value, "REPLACE");
}
int MessageStore::Update(Message* value) {
  return InsertOrReplace(value, "UPDATE");
}

int MessageStore::InsertOrReplace(Message* value, std::string cmd) {
  if (never_save_) {
    return SQLITE_MISUSE;
  }
  std::string tablename = value->GetDescriptor()->name();
  VLOG(30) << "InsertOrReplace " << value->DebugString();

  const Reflection* reflection = value->GetReflection();
  vector<const FieldDescriptor *> fields;
  vector<std::string> field_names, fmt_string, update_portion;
  reflection->ListFields(*value, &fields);
  for (vector<const FieldDescriptor *>::iterator it = fields.begin() ; it != fields.end(); ++it) {
    const FieldDescriptor* fd = *it;
    if (fd->is_repeated()) {
      continue; // we handle these later
    }
    field_names.push_back(fd->name());
    fmt_string.push_back("?");
    update_portion.push_back(fd->name() + " = ? ");
  }
  std::string field_name_str;
  field_name_str = boost::algorithm::join(field_names, ",");
  std::string fmt_str = boost::algorithm::join(fmt_string, ",");
  std::string update_str = boost::algorithm::join(update_portion, ",");

  // We may or may not even be a table with an id
  int local_id = 0;

  if (value->GetDescriptor()->field(0)->type() == FieldDescriptor::TYPE_INT64) {
    if (reflection->HasField(*value, value->GetDescriptor()->field(0))) {
      local_id = reflection->GetInt64(*value, value->GetDescriptor()->field(0));
    } else {
      // We will have an ID after the insert, but don't know.
      local_id = -1;
    }
  }

  CHECK(SQLITE_OK == sqlite3_exec(db_, "BEGIN TRANSACTION", NULL, NULL, NULL)) << sqlite3_errmsg(db_);
  std::string query;

  if (cmd == "INSERT" || cmd == "REPLACE") { 
    query = cmd +  " INTO " + tablename + " ("+field_name_str+") VALUES ("+fmt_str+")";
  } else if (cmd == "UPDATE") {
    query = "UPDATE " + tablename + " SET " + update_str + " WHERE " + value->GetDescriptor()->field(0)->name() + " = ?";
    CHECK(local_id != -1) << "Cannot do an update without an ID.";
  }

  sqlite3_stmt *ps;

  VLOG(99) << query;
  CHECK(SQLITE_OK == sqlite3_prepare_v2(db_, query.c_str(), -1, &ps, NULL)) << sqlite3_errmsg(db_);
  
  int next_field = BindFromFields(*value, ps);  

  if (cmd == "UPDATE") {
    sqlite3_bind_int64(ps, next_field, local_id);
  }

  switch (sqlite3_step(ps)) {
  case SQLITE_CONSTRAINT:
    CHECK(SQLITE_OK == sqlite3_exec(db_, "ROLLBACK", NULL, NULL, NULL));
    sqlite3_finalize(ps);
    throw ConstraintException;
    break;
  case SQLITE_OK:
  case SQLITE_DONE:
    break;
  default:
    CHECK(false) << sqlite3_errmsg(db_);
    break;
  }
  CHECK(SQLITE_OK == sqlite3_finalize(ps));
  if (local_id == -1) {
    local_id = sqlite3_last_insert_rowid(db_);
    reflection->SetInt64(value, value->GetDescriptor()->field(0), local_id);
  } 

  for (vector<const FieldDescriptor *>::iterator it = fields.begin(); it != fields.end(); ++it) {
    const FieldDescriptor* fd = *it;
    if (!fd->is_repeated()) {
      continue; // we handled these in the root insert
    }
    std::string query;
    if (cmd == "INSERT" || cmd == "UPDATE") {
      query = "INSERT OR IGNORE";
    } else {
      query = "REPLACE";
    }
    query += " INTO " + tablename + "_" + fd->name() + " (" + value->GetDescriptor()->field(0)->name() + "," + fd->name() + " ) VALUES (?, ?)";
    VLOG(9) << query;
    VLOG(9) << field_names[0];
    CHECK(SQLITE_OK == sqlite3_prepare_v2(db_, query.c_str(), -1, &ps, NULL)) << sqlite3_errmsg(db_);
    for (int i = 0; i < reflection->FieldSize(*value, fd); ++i) {
      sqlite3_bind_int64(ps, 1, local_id);
      sqlite3_bind_int64(ps, 2, reflection->GetRepeatedInt64(*value, fd, i));
      switch (sqlite3_step(ps)) {
        case SQLITE_CONSTRAINT:
          VLOG(5) << "constraint: rollback";
          CHECK(SQLITE_OK == sqlite3_finalize(ps));
          CHECK(SQLITE_OK == sqlite3_exec(db_, "ROLLBACK", NULL, NULL, NULL));
          throw ConstraintException;
          break;
        case SQLITE_DONE:
          break;
        default:
          CHECK(false) << "Unknown error " << sqlite3_errmsg(db_);
      }
      CHECK(SQLITE_OK == sqlite3_reset(ps));
    }
    VLOG(5) << "About to finalize";
    CHECK(SQLITE_OK == sqlite3_finalize(ps)) << sqlite3_errmsg(db_);
  }
  VLOG(5) << "About to commit";
  bool result = sqlite3_exec(db_, "COMMIT", NULL, NULL, NULL);
  if (result != SQLITE_OK) {
    CHECK(SQLITE_OK == sqlite3_exec(db_, "ROLLBACK", NULL, NULL, NULL));
    throw ConstraintException;
  }
  return result;
}
int MessageStore::BindFromFields(const Message& object, sqlite3_stmt *ps) { 
  const Reflection* reflection = object.GetReflection();
  vector<const FieldDescriptor *> fields;
  reflection->ListFields(object, &fields);
  int i = 1;
  for (vector<const FieldDescriptor *>::iterator it = fields.begin() ; it != fields.end(); ++it, ++i) {
    const FieldDescriptor* fd = *it;
    if (fd->is_repeated()) {
      continue;
    }
    std::string fieldval;
    switch (fd->type()) {
      case FieldDescriptor::TYPE_INT32:
        sqlite3_bind_int(ps, i, reflection->GetInt32(object, fd));
        break;
      case FieldDescriptor::TYPE_INT64:
        sqlite3_bind_int64(ps, i, reflection->GetInt64(object, fd));
        break;
      case FieldDescriptor::TYPE_BYTES:
      case FieldDescriptor::TYPE_STRING:
        fieldval = reflection->GetString(object, fd);
        sqlite3_bind_text(ps, i, fieldval.c_str(), fieldval.size(), SQLITE_TRANSIENT);
        break;
      default:
        CHECK(false) << "Unsupported " << fd->type();
        break;
    }
  }
  return i;
}
bool MessageStore::ProtoFromRows(sqlite3_stmt *ps, Message *result) {
  const Reflection* reflection = result->GetReflection();
  if (SQLITE_ROW == sqlite3_step(ps)) {
    const char *blob;
    vector<string> ids;
    std::string stringblob;
    for(int i = 0; i < sqlite3_column_count(ps); ++i) {
      //CHECK(sqlite3_column_table_name(ps, i) == table_) << sqlite3_column_table_name(ps, i) << " does not match " << table_;
      const FieldDescriptor *fd = desc_->FindFieldByName(sqlite3_column_name(ps, i));
      CHECK(fd != NULL) << "No field " << sqlite3_column_name(ps, i) << " found on proto.";
      switch (fd->type()) {
      case FieldDescriptor::TYPE_INT32:
        reflection->SetInt32(result, fd, sqlite3_column_int(ps, i));
        break;
      case FieldDescriptor::TYPE_INT64:
        if (!fd->is_repeated()) {
          reflection->SetInt64(result, fd, sqlite3_column_int(ps, i));
          break;
        }
        // We can do joins by getting a group_concat of IDs via a view, so special case that
        blob = (char *)sqlite3_column_blob(ps, i);
        stringblob = string(blob, sqlite3_column_bytes(ps, i));
        boost::algorithm::split(ids, stringblob, boost::is_any_of(","));
        for (vector<string>::iterator it = ids.begin(); it != ids.end(); ++it) {
          reflection->AddInt64(result, fd, atoll(it->c_str()));
        }
        break;
      case FieldDescriptor::TYPE_STRING:
      case FieldDescriptor::TYPE_BYTES:
        blob = (char *) sqlite3_column_blob(ps, i);
        stringblob = string(blob, sqlite3_column_bytes(ps, i));
        reflection->SetString(result, fd, stringblob);
        break;
      default:
        CHECK(false) << "Unknown type " << fd->type();
      }
    }
    return true;
  }
  return false;
}

} // namespace automation
