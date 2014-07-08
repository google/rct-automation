# Copyright 2012-2014 Google, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

CPPFLAGS=-I/usr/include/jsoncpp -I/usr/local/include/jsoncpp -Iglog/src/ -Igflags/src/ -Ithird_party/protobuf-to-jsoncpp/
COMMON_OBJS=actions.o automationstate.o db.o http.o mplayersession.o messagestore.o playableitem.o playlist.o requirementengine.o webapi.o glog/.libs/libglog.a gflags/.libs/libgflags.a playlist.pb.o playableitem.pb.o protostore.pb.o playerstate.pb.o requirement.pb.o sql.pb.o third_party/protobuf-to-jsoncpp/json_protobuf.o
ACMD_OBJS=$(COMMON_OBJS) acmd-main.o
AUTOMATION_OBJS=$(COMMON_OBJS) automation.o
LDFLAGS=-L/usr/lib -L/usr/local/lib  -lboost_system-mt -lboost_regex-mt -lboost_thread-mt -lpion-net -ljsoncpp -lpion-common -llog4cpp -lsqlite3 -lprotobuf -lboost_system-mt -lboost_regex-mt -lboost_thread-mt -lpion-net -ljsoncpp -lpion-common -llog4cpp -lsqlite3 -rdynamic -ljsoncpp

%.pb.h: %.proto
	protoc $< --cpp_out=.
%.pb.cc: %.proto
	protoc $< --cpp_out=.


all: submodules protos automation acmd

clean:
	-rm -r *.o automation *.pb.h acmd *.pb.cc
distclean: clean
	-rm -r glog gflags

submodules:
	git submodule init && git submodule update

protos: playlist.pb.h playableitem.pb.h protostore.pb.h playerstate.pb.h requirement.pb.h sql.pb.h

automation: submodules protos glog/.libs/libglog.a gflags/.libs/libgflags.a $(AUTOMATION_OBJS)
	    $(CXX) $(AUTOMATION_OBJS) -o automation glog/.libs/libglog.a $(LDFLAGS)

acmd: submodules protos $(ACMD_OBJS)
	    $(CXX) $(ACMD_OBJS) -o acmd glog/.libs/libglog.a $(LDFLAGS)

glog/.libs/libglog.a:
	    cd glog && ./configure && make

gflags/.libs/libgflags.a:
	    cd gflags && ./configure && make
