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


#include <iostream>
#include <pion/net/HTTPServer.hpp>
#include <pion/net/HTTPTypes.hpp>
#include <pion/net/HTTPRequest.hpp>
#include <pion/net/HTTPResponseWriter.hpp>
#include "http.h"
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <google/protobuf/message.h>
#include <json/json.h>
#include "json_protobuf.h"

using namespace std;
using namespace pion;
using namespace pion::net;

void WebCommand::handle_command(HTTPRequestPtr& http_request, TCPConnectionPtr& tcp_conn) {
  HTTPResponseWriterPtr
    writer(HTTPResponseWriter::create(tcp_conn,
                                      *http_request,
                                      boost::bind(&TCPConnection::finish,
                                                  tcp_conn)));
  HTTPResponse& r = writer->getResponse();
  VLOG(60) << "Resource: " << http_request->getOriginalResource();
  VLOG(60) << "Query string: " << http_request->getQueryString();

  r.setStatusCode(HTTPTypes::RESPONSE_CODE_OK);
  r.setStatusMessage(HTTPTypes::RESPONSE_MESSAGE_OK);
  params_ = http_request->getQueryParams(); 
  request_ = http_request;
  writer_ = writer;
  SSL *cert = tcp_conn->getSSLSocket().impl()->ssl;

  if (cert) {
    X509 *info = SSL_get_peer_certificate(cert);
    if (info) {
      char buf[512];
      X509_NAME_oneline(X509_get_subject_name(info), buf, sizeof buf);
      remote_user_ = buf;
    }
  }

  LOG(INFO) << "API command " << request_->getResource() << " from " << tcp_conn->getRemoteIp() << " " << remote_user_ << " running now...";
  this->handle_command(http_request, writer, remote_user_);

  writer->send();
}

void WebCommand::ReturnMessage(const ::google::protobuf::Message& value) {
  std::string format;
  if (params_.count("format")) {
    format = params_.equal_range("format").first->second;
  } else {
    format = "pb";
  }
  // TODO throw up some headers for the json users to know more about what they have
  if (format == "debugpb") {
    writer_ << value.DebugString();  
  } else if (format == "json") {
    writer_->getResponse().setContentType("application/json");
    Json::Value pt;
    json_protobuf::convert_to_json(value, pt);
    writer_ << pt;
    return;
  } else {
    writer_->getResponse().setContentType("application/x-protobuf; desc=\"/pb/"+value.GetTypeName()+".desc\"; messageType=\""+value.GetTypeName()+"\";);");
    writer_ << value.SerializeAsString();
    VLOG(5) << "Sent proto of size " << value.SerializeAsString().size() << " on wire";
  }
} 
