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

DEFINE_string(bumpers, "", "Name of playlist which contains bumpers.  If empty, use all playableitems instead.");
DEFINE_bool(webapi, true, "If false, do not setup a web backend.");
DEFINE_int32(port, 8080, "Port to listen on");

DEFINE_bool(secure, true, "If true, only clients with valid SSL certs can perform requests.");
DEFINE_string(ssl_ca, "", "If set, path to the trusted CA for client authentication.");
DEFINE_string(ssl_crt, "", "If set, path to our SSL certificate. (PEM-encoded)");
DEFINE_string(ssl_key, "", "If set, path to our SSL host key. (PEM-encoded)");

// If you do have some need to go above 50, you'll want to boost AUTOMATION_MAX_FD as well.  Note we give ourselves
// 5 in this calculation, but we probably are using ~half that.  So there's a bit of a safety margin with 50, here,
// which is useful in case we end up opening up lots of file descriptors for some new feature.
DEFINE_int32(threadcount, 8, "Number of threads to have available for HTTP requests. Set this above 50 at your own peril.");
DEFINE_string(interface, "127.0.0.1", "IP of interface to listen on");
DEFINE_bool(doinit, true, "If true, we play commands marked @reboot on startup, pre-webserver standup.");
DEFINE_bool(fast_shutdown, false, "If true, shutdown immediately on exit request. "
                                  "Otherwise, attempt to defer shutdown until after the track ends.");

int shutdown_requested = 0;

void signalhandler(int);
void signalhandler(int signal) {
  switch(signal) {
   case SIGPIPE:
    break;
   case SIGUSR1:
    break;
   default:
    shutdown_requested = 1;
    if (FLAGS_fast_shutdown) {
      exit(0);
    }
  }
}


int main(int argc, char **argv) {
  google::InitGoogleLogging(argv[0]);
  google::SetUsageMessage("Usage");
  google::ParseCommandLineFlags(&argc, &argv, true);
  google::InstallFailureSignalHandler();
  std::srand(time(NULL));

  struct sigaction new_action;
  new_action.sa_handler = signalhandler;
  sigemptyset(&new_action.sa_mask);
  new_action.sa_flags = 0; //SA_NODEFER;

  struct sigaction ignored_signals;
  sigemptyset(&ignored_signals.sa_mask);
  ignored_signals.sa_flags = 0;
  ignored_signals.sa_handler = SIG_IGN;

  sigaction(SIGTERM, &new_action, NULL);
  sigaction(SIGINT, &new_action, NULL);
  sigaction(SIGUSR1, &new_action, NULL);
  sigaction(SIGPIPE, &ignored_signals, NULL); 

  RequirementEngine::CheckValidity();

  LOG(INFO) << "automation-ng starting up";

  struct rlimit filelimit;
  getrlimit(RLIMIT_NOFILE, &filelimit);
  filelimit.rlim_max = AUTOMATION_MAX_FD+1;
  setrlimit(RLIMIT_NOFILE, &filelimit);

  std::srand(time(NULL));
  sqlite3 *db;

  db = DatabaseOpen();
  if (sqlite3_exec(db, "DELETE FROM PlaylistLock;", NULL, NULL, NULL) != SQLITE_OK) {
    LOG(WARNING) << "Unable to truncate locked playlist list.";
  }

  WebAPI::ReadFromDatabase(db);
  MplayerSession mp;
  
  fclose(stdin);

  AutomationState automation(db, &mp); 
  if (FLAGS_doinit) {
    automation.get_requirement_engine()->HandleReboot();
  }

  HTTPServerPtr webapi_server;
  PionSingleServiceScheduler scheduler;
  CHECK(!FLAGS_secure || (!FLAGS_ssl_key.empty() && !FLAGS_ssl_crt.empty())) << "Must specify --nosecure or (--ssl_key and --ssl_cert)";

  if (FLAGS_webapi) {
    boost::asio::ip::tcp::endpoint endpoint;
    endpoint.address(boost::asio::ip::address::from_string(FLAGS_interface));
    endpoint.port(FLAGS_port);
    scheduler.setNumThreads(FLAGS_threadcount);
    webapi_server.reset(new HTTPServer(scheduler, endpoint));
    if (!FLAGS_ssl_key.empty()) {
      webapi_server->setSSLFlag(true);
      if (!FLAGS_ssl_ca.empty()) {
        webapi_server->getSSLContext().load_verify_file(FLAGS_ssl_ca);
      }
      webapi_server->getSSLContext().use_certificate_file(FLAGS_ssl_crt, boost::asio::ssl::context::pem);
      webapi_server->getSSLContext().use_private_key_file(FLAGS_ssl_key, boost::asio::ssl::context::pem);
      webapi_server->getSSLContext().set_options(boost::asio::ssl::context::default_workarounds
                | boost::asio::ssl::context::no_sslv2
                | boost::asio::ssl::context::single_dh_use);
      if (FLAGS_secure) {
        webapi_server->getSSLContext().set_verify_mode(boost::asio::ssl::context::verify_peer | boost::asio::ssl::context::verify_fail_if_no_peer_cert);
      }
    }
    WebAPI::Registrar::CallbackMap &cm = WebAPI::Registrar::get_callbackmap();
    for (WebAPI::Registrar::CallbackMap::iterator it = cm.begin(); it != cm.end(); ++it) {
      webapi_server->addResource(it->first, it->second);
    }

    try {
      webapi_server->start();
    } catch (std::exception& e) {
      std::cerr << "Failed running the HTTP service due to " <<  e.what();
      exit(-1);
    }
  }

  LOG(INFO) << "Entering main loop";
  while (!shutdown_requested) {
    if (!automation.RunOnce()) {
      LOG(ERROR) << "Automation::RunOnce returned false";
    }
  }
  LOG(INFO) << "Main loop exit.";

  webapi_server.reset();
  wait(NULL);
}

