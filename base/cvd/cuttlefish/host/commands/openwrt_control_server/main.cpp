/*
 *
 * Copyright (C) 2023 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <regex>
#include <string>
#include <thread>

#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "fmt/format.h"
#include "gflags/gflags.h"
#include "google/protobuf/empty.pb.h"
#include "grpcpp/ext/proto_server_reflection_plugin.h"
#include "grpcpp/grpcpp.h"
#include "grpcpp/health_check_service_interface.h"

#include "cuttlefish/files/file_exists.h"
#include "cuttlefish/host/commands/openwrt_control_server/openwrt_control.grpc.pb.h"
#include "cuttlefish/host/libs/web/http_client/curl_http_client.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/host/libs/web/http_client/http_json.h"
#include "cuttlefish/result/result.h"

using absl::StartsWith;
using google::protobuf::Empty;
using google::protobuf::RepeatedPtrField;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using grpc::StatusCode;
using openwrtcontrolserver::LuciRpcReply;
using openwrtcontrolserver::LuciRpcRequest;
using openwrtcontrolserver::OpenwrtControlService;
using openwrtcontrolserver::OpenwrtIpaddrReply;

DEFINE_string(grpc_uds_path, "", "grpc_uds_path");
DEFINE_bool(bridged_wifi_tap, false,
            "True for using cvd-wtap-XX, false for using cvd-wifiap-XX");
DEFINE_string(webrtc_device_id, "", "The device ID in WebRTC like cvd-1");
DEFINE_string(launcher_log_path, "", "File path for launcher.log");
DEFINE_string(openwrt_log_path, "", "File path for crosvm_openwrt.log");

namespace cuttlefish {
namespace {

constexpr char kErrorMessageRpc[] = "Luci RPC request failed";
constexpr char kErrorMessageRpcAuth[] = "Luci authentication request failed";

static Status ErrorResultToStatus(const std::string_view prefix,
                                  const StackTraceError& error) {
  std::string msg = fmt::format("{}:\n\n{}", prefix, error.FormatForEnv(false));
  return Status(StatusCode::UNAVAILABLE, msg);
}

static int ParseInstanceNumFromDeviceId(const std::string& device_id) {
  auto pos = device_id.rfind('-');
  int num = 1;
  if (pos != std::string::npos &&
      absl::SimpleAtoi(device_id.substr(pos + 1), &num) && num >= 1 &&
      num <= 128) {
    return num;
  }
  return 1;
}

class OpenwrtControlServiceImpl final : public OpenwrtControlService::Service {
 public:
  OpenwrtControlServiceImpl(HttpClient& http_client)
      : http_client_(http_client) {
    ipv6_provisioning_thread_ = std::thread([this]() {
      ProvisionOpenwrtIpv6Loop();
    });
    ipv6_provisioning_thread_.detach();
  }

  Status LuciRpc(ServerContext* context, const LuciRpcRequest* request,
                 LuciRpcReply* response) override {
    std::lock_guard<std::mutex> lock(rpc_mutex_);
    // Update authentication key when it's empty.
    if (auth_key_.empty()) {
      Result<void> auth_res = UpdateLuciRpcAuthKey();
      if (!auth_res.has_value()) {
        return ErrorResultToStatus(kErrorMessageRpcAuth, auth_res.error());
      }
    }

    auto reply = RequestLuciRpc(request->subpath(), request->method(),
                                ToVector(request->params()));

    // When RPC request fails, update authentication key and retry once.
    if (!reply.has_value()) {
      Result<void> auth_res = UpdateLuciRpcAuthKey();
      if (!auth_res.has_value()) {
        return ErrorResultToStatus(kErrorMessageRpcAuth, auth_res.error());
      }
      reply = RequestLuciRpc(request->subpath(), request->method(),
                             ToVector(request->params()));
      if (!reply.has_value()) {
        return ErrorResultToStatus(kErrorMessageRpc, reply.error());
      }
    }

    Json::FastWriter writer;
    response->set_id((*reply)["id"].asInt());
    response->set_error((*reply)["error"].asString());
    response->set_result(writer.write((*reply)["result"]));

    // If the caller restarted OpenWrt network service (e.g. on snapshot restore),
    // re-apply the IPv6 configuration.
    if (request->subpath() == "sys" && request->method() == "exec" &&
        request->params_size() > 0 &&
        absl::StrContains(request->params(0), "network")) {
      (void)ConfigureOpenwrtIpv6Locked();
    }

    return Status::OK;
  }

  Status OpenwrtIpaddr(ServerContext* context, const Empty* request,
                       OpenwrtIpaddrReply* response) override {
    // TODO(seungjaeyoo) : Find IP address from crosvm_openwrt.log when using
    // cvd-wtap-XX after disabling DHCP inside OpenWRT in bridged_wifi_tap mode.
    Result<std::string> ipaddr = FindIpaddrLauncherLog();
    if (!ipaddr.has_value()) {
      return ErrorResultToStatus("Failed to get Openwrt IP address",
                                 ipaddr.error());
    }
    response->set_ipaddr(*ipaddr);
    return Status::OK;
  }

 private:
  void ProvisionOpenwrtIpv6Loop() {
    // Poll OpenWrt Luci RPC and ensure IPv6 SLAAC/RA + routing are configured
    // both before and after OpenWrt's initial hostapd reboot (b/305102099).
    int success_count = 0;
    for (int attempt = 0; attempt < 60 && success_count < 3; ++attempt) {
      std::this_thread::sleep_for(std::chrono::seconds(2));
      std::lock_guard<std::mutex> lock(rpc_mutex_);
      if (ConfigureOpenwrtIpv6Locked().has_value()) {
        ++success_count;
      }
    }
  }

  std::string BuildOpenwrtIpv6SetupCommand() const {
    const int id = ParseInstanceNumFromDeviceId(FLAGS_webrtc_device_id);
    const std::string wan_gua_gw =
        FLAGS_bridged_wifi_tap
            ? "2001:db8:cf:20::1"
            : fmt::format("2001:db8:cf:22:{}::1", id);
    const std::string wan_gua_addr =
        FLAGS_bridged_wifi_tap
            ? fmt::format("2001:db8:cf:20::{}/64", id + 1)
            : fmt::format("2001:db8:cf:22:{}::2/64", id);
    const std::string wan_ula_gw =
        FLAGS_bridged_wifi_tap
            ? "fd00:cf:20::1"
            : fmt::format("fd00:cf:22:{}::1", id);
    const std::string wan_ula_addr =
        FLAGS_bridged_wifi_tap
            ? fmt::format("fd00:cf:20::{}/64", id + 1)
            : fmt::format("fd00:cf:22:{}::2/64", id);
    const std::string wifi0_gua_addr =
        (id == 1) ? "2001:db8:cf:23::1/64"
                  : fmt::format("2001:db8:cf:23:{}::1/64", id);
    const std::string wifi0_ula_addr =
        fmt::format("fd00:cf:23:{}::1/64", id);

    return fmt::format(
        "if ! ip -6 addr show dev br-wifi0 2>/dev/null | grep -q '2001:db8:cf:23'; then "
        "uci set network.globals.ula_prefix='fd00:cf:23::/48'; "
        "uci set network.wan.ip6addr='{0}'; "
        "uci set network.wan.ip6gw='{1}'; "
        "uci set network.wan.ip6prefix='2001:db8:cf:23::/48'; "
        "uci set network.wifi0.ip6assign='64'; "
        "uci set network.wifi0.ip6hint='{4}'; "
        "uci delete network.wifi0.ip6addr 2>/dev/null || true; "
        "uci add_list network.wifi0.ip6addr='{2}'; "
        "uci add_list network.wifi0.ip6addr='{3}'; "
        "uci set network.wifi1.ip6assign='64'; "
        "uci commit network; "
        "uci set dhcp.wifi0.dhcpv6='server'; "
        "uci set dhcp.wifi0.ra='server'; "
        "uci set dhcp.wifi0.ra_slaac='1'; "
        "uci set dhcp.wifi0.ra_default='1'; "
        "uci set dhcp.wifi0.ra_maxinterval='10'; "
        "uci set dhcp.wifi0.ra_mininterval='3'; "
        "uci delete dhcp.wifi0.dns 2>/dev/null || true; "
        "uci add_list dhcp.wifi0.dns='2001:4860:4860::8888'; "
        "uci add_list dhcp.wifi0.dns='2001:4860:4860::8844'; "
        "uci set dhcp.wifi1.dhcpv6='server'; "
        "uci set dhcp.wifi1.ra='server'; "
        "uci set dhcp.wifi1.ra_slaac='1'; "
        "uci set dhcp.wifi1.ra_default='1'; "
        "uci commit dhcp; "
        "ubus call network reload 2>/dev/null || true; "
        "fi; "
        "sysctl -w net.ipv6.conf.all.forwarding=1 >/dev/null 2>&1 || true; "
        "sysctl -w net.ipv6.conf.default.forwarding=1 >/dev/null 2>&1 || true; "
        "ip -6 addr replace {0} dev br-lan 2>/dev/null || true; "
        "ip -6 addr replace {5} dev br-lan 2>/dev/null || true; "
        "ip -6 addr replace {2} dev br-wifi0 2>/dev/null || true; "
        "ip -6 addr replace {3} dev br-wifi0 2>/dev/null || true; "
        "ip -6 route replace default via {1} dev br-lan 2>/dev/null || true; "
        "ip -6 route append default via {6} dev br-lan 2>/dev/null || true; "
        "nft insert rule inet fw4 forward accept 2>/dev/null || nft flush ruleset 2>/dev/null || true; "
        "ip6tables -P FORWARD ACCEPT 2>/dev/null || true; "
        "ip6tables -I FORWARD 1 -j ACCEPT 2>/dev/null || true; "
        "/etc/init.d/odhcpd restart >/dev/null 2>&1 || true",
        wan_gua_addr, wan_gua_gw, wifi0_gua_addr, wifi0_ula_addr, id,
        wan_ula_addr, wan_ula_gw);
  }

  Result<void> ConfigureOpenwrtIpv6Locked() {
    if (auth_key_.empty()) {
      CF_EXPECT(UpdateLuciRpcAuthKey());
    }
    const std::string cmd = BuildOpenwrtIpv6SetupCommand();
    auto reply = RequestLuciRpc("sys", "exec", {cmd});
    if (!reply.has_value()) {
      CF_EXPECT(UpdateLuciRpcAuthKey());
      reply = RequestLuciRpc("sys", "exec", {cmd});
      CF_EXPECT(std::move(reply));
    }
    return {};
  }

  template <typename T>
  std::vector<T> ToVector(const RepeatedPtrField<T>& repeated_field) {
    std::vector<T> vec;
    for (const auto& value : repeated_field) {
      vec.push_back(value);
    }
    return vec;
  }

  Result<std::string> LuciRpcAddress(const std::string& subpath) {
    auto ipaddr = CF_EXPECT(FindIpaddrLauncherLog());
    return "http://" + ipaddr + "/devices/" + FLAGS_webrtc_device_id +
           "/openwrt/cgi-bin/luci/rpc/" + subpath;
  }

  Result<std::string> LuciRpcAddress(const std::string& subpath,
                                     const std::string& auth_key) {
    auto addr_without_auth = CF_EXPECT(LuciRpcAddress(subpath));
    return addr_without_auth + "?auth=" + auth_key;
  }

  Json::Value LuciRpcData(const std::string& method,
                          const std::vector<std::string>& params) {
    Json::Value data;
    data["method"] = method;
    Json::Value params_json_obj(Json::arrayValue);
    for (const auto& param : params) {
      params_json_obj.append(param);
    }
    data["params"] = params_json_obj;
    return data;
  }

  Json::Value LuciRpcData(int id, const std::string& method,
                          const std::vector<std::string>& params) {
    Json::Value data = LuciRpcData(method, params);
    data["id"] = id;
    return data;
  }

  Result<void> UpdateLuciRpcAuthKey() {
    auto auth_url = CF_EXPECT(LuciRpcAddress("auth"));
    auto auth_data = LuciRpcData(1, "login", {"root", "password"});
    auto auth_reply =
        CF_EXPECT(HttpPostToJson(http_client_, auth_url, auth_data, header_));
    if (auth_reply.data["error"].isString()) {
      CF_EXPECT(!StartsWith(auth_reply.data["error"].asString(),
                            "Failed to parse json:"),
                kErrorMessageRpcAuth);
    }
    CF_EXPECT(auth_reply.data["result"].isString(),
              "Reply doesn't contain result");
    auth_key_ = auth_reply.data["result"].asString();

    return {};
  }

  Result<Json::Value> RequestLuciRpc(const std::string& subpath,
                                     const std::string& method,
                                     const std::vector<std::string>& params) {
    auto url = CF_EXPECT(LuciRpcAddress(subpath, auth_key_));
    auto data = LuciRpcData(method, params);
    auto reply = CF_EXPECT(HttpPostToJson(http_client_, url, data, header_));
    if (reply.data["error"].isString()) {
      CF_EXPECT(
          !StartsWith(reply.data["error"].asString(), "Failed to parse json:"),
          kErrorMessageRpc);
    }
    return reply.data;
  }

  Result<std::string> FindIpaddrLauncherLog() {
    if (!FileExists(FLAGS_launcher_log_path)) {
      return CF_ERR("launcher.log doesn't exist");
    }

    std::regex re("wan_ipaddr=[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+");
    std::smatch matches;
    std::ifstream ifs(FLAGS_launcher_log_path);
    std::string line, last_match;
    while (std::getline(ifs, line)) {
      if (std::regex_search(line, matches, re)) {
        last_match = matches[0];
      }
    }

    if (last_match.empty()) {
      return CF_ERR("IP address is not found from launcher.log");
    } else {
      return last_match.substr(last_match.find('=') + 1);
    }
  }

  HttpClient& http_client_;
  const std::vector<std::string> header_{"Content-Type: application/json"};
  std::string auth_key_;
  std::mutex rpc_mutex_;
  std::thread ipv6_provisioning_thread_;
};

void RunServer() {
  std::string server_address("unix:" + FLAGS_grpc_uds_path);
  std::unique_ptr<HttpClient> http_client = CurlHttpClient();
  OpenwrtControlServiceImpl service(*http_client);

  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  ServerBuilder builder;
  // Listen on the given address without any authentication mechanism.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  // Register "service" as the instance through which we'll communicate with
  // clients. In this case it corresponds to an *synchronous* service.
  builder.RegisterService(&service);
  // Finally assemble the server.
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;

  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  server->Wait();
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv) {
  ::gflags::ParseCommandLineFlags(&argc, &argv, true);
  cuttlefish::RunServer();

  return 0;
}
