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

#ifndef _PROTOSTORE_H
#define _PROTOSTORE_H

#include <stdio.h>
#include <glog/logging.h>
#include "sqlite3.h"
#include <boost/thread/mutex.hpp>
#include <google/protobuf/descriptor.h>
#include <string>
#include <vector>
#include "messagestore.h"
#include "protostore.pb.h"

using namespace google::protobuf;

namespace automation {

// Instead of having everyone use the MessageStore directly, we wrap it with a ProtoStore which
// is type-aware.  This lets us have type safety on the cheap.  We bother with having it
// be a separate class, in order to minimize the size of the template (and therefore
// speed up compliation time)
template<class TypeName> class ProtoStore : public automation::MessageStore {
 public: 
  ProtoStore(sqlite3 *db) : 
    MessageStore(db, TypeName().GetDescriptor(), TypeName().GetDescriptor()->name()) {
  }

  ProtoStore(sqlite3 *db, const std::string& tablename) :
    MessageStore(db, TypeName().GetDescriptor(), tablename) {
  }

  bool LoadAll(std::vector<TypeName> *result, int64_t limit, int64_t offset) {
    std::string query = "SELECT * from " + table_ + " LIMIT ? OFFSET ?";

    sqlite3_stmt *ps;
    CHECK(SQLITE_OK == sqlite3_prepare_v2(db_, query.c_str(), -1, &ps, NULL)) << sqlite3_errmsg(db_);
    sqlite3_bind_int64(ps, 1, limit);
    sqlite3_bind_int64(ps, 2, offset);

    TypeName temp;
    while (ProtoFromRows(ps, &temp)) {
      result->push_back(temp);
      VLOG(90) << "Adding " << temp.DebugString();
      temp.Clear();
    }
    CHECK(SQLITE_OK == sqlite3_reset(ps));
    return result->size();
  }
};

template<class TypeName> class ThreadSafeProto : public ProtoStore<TypeName> {
 public:
  ThreadSafeProto(sqlite3 *db) :
    ProtoStore<TypeName>(db) {
  }

  bool Fetch(sqlite3_int64 id) {
    boost::mutex::scoped_lock lock(mutex_);
    return ProtoStore<TypeName>::LoadById(&canonical_, id);
  }

  void CopyTo(TypeName *result) {
    boost::mutex::scoped_lock lock(mutex_);
    result->CopyFrom(canonical_);
  }
  void CopyFrom(const TypeName& candidate) {
    boost::mutex::scoped_lock lock(mutex_);
    canonical_.CopyFrom(candidate);
  }
  void MergeFrom(const TypeName& candidate) {
    boost::mutex::scoped_lock lock(mutex_);
    canonical_.MergeFrom(candidate);
  }
  int Replace() {
    boost::mutex::scoped_lock lock(mutex_);
    return ProtoStore<TypeName>::Replace(&canonical_);
  }
  int Update() {
    boost::mutex::scoped_lock lock(mutex_);
    return ProtoStore<TypeName>::Update(&canonical_);
  }
  void Clear() {
    boost::mutex::scoped_lock lock(mutex_);
    return canonical_.Clear();
  }
  int Insert() {
    boost::mutex::scoped_lock lock(mutex_);
    return ProtoStore<TypeName>::Insert(&canonical_);
  }
  const TypeName& data() {
    return canonical_;
  }
  TypeName& mutable_data() {
    return canonical_;
  }
 protected:
  mutable boost::mutex mutex_; // Guards canonical_
  TypeName canonical_;
};

class BasicProtoStore  {
 private:
  sqlite3 *db_;
  ProtoStore<automation::ProtoTable> pstore_;

 public: 
  BasicProtoStore(sqlite3 *db) :
    db_(db),
    pstore_(db) {

  }
  template<class TypeName> bool Load(TypeName* lookup) {
    automation::ProtoTable request;
    request.set_label(lookup->GetTypeName()); 
    pstore_.Load(&request);
    if (request.has_data()) {
      lookup->ParseFromString(request.data());
    }
    return true;
  }
  template<class TypeName> bool Save(TypeName* save) {
    automation::ProtoTable request;
    request.set_label(save->GetTypeName());
    request.set_data(save->SerializeAsString());
    pstore_.Replace(&request);
    return true;
  }
};

}
#endif
