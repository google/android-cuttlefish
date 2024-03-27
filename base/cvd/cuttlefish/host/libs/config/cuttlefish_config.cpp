/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "host/libs/config/cuttlefish_config.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string>
#include <time.h>

#include <android-base/strings.h>
#include <android-base/logging.h>
#include <json/json.h>

#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "host/libs/vm_manager/crosvm_manager.h"
#include "host/libs/vm_manager/gem5_manager.h"
#include "host/libs/vm_manager/qemu_manager.h"

namespace cuttlefish {
namespace {

const char* kInstances = "instances";

}  // namespace

const char* const kVhostUserVsockModeAuto = "auto";
const char* const kVhostUserVsockModeTrue = "true";
const char* const kVhostUserVsockModeFalse = "false";

const char* const kGpuModeAuto = "auto";
const char* const kGpuModeDrmVirgl = "drm_virgl";
const char* const kGpuModeGfxstream = "gfxstream";
const char* const kGpuModeGfxstreamGuestAngle = "gfxstream_guest_angle";
const char* const kGpuModeGfxstreamGuestAngleHostSwiftShader =
    "gfxstream_guest_angle_host_swiftshader";
const char* const kGpuModeGuestSwiftshader = "guest_swiftshader";
const char* const kGpuModeNone = "none";

const char* const kGpuVhostUserModeAuto = "auto";
const char* const kGpuVhostUserModeOn = "on";
const char* const kGpuVhostUserModeOff = "off";

const char* const kHwComposerAuto = "auto";
const char* const kHwComposerDrm = "drm";
const char* const kHwComposerRanchu = "ranchu";
const char* const kHwComposerNone = "none";

std::string DefaultEnvironmentPath(const char* environment_key,
                                   const char* default_value,
                                   const char* subpath) {
  return StringFromEnv(environment_key, default_value) + "/" + subpath;
}

bool IsRestoring(const CuttlefishConfig& config) {
  return FileExists(config.AssemblyPath("restore"));
}
ConfigFragment::~ConfigFragment() = default;

static constexpr char kFragments[] = "fragments";
bool CuttlefishConfig::LoadFragment(ConfigFragment& fragment) const {
  if (!dictionary_->isMember(kFragments)) {
    LOG(ERROR) << "Fragments member was missing";
    return false;
  }
  const Json::Value& json_fragments = (*dictionary_)[kFragments];
  if (!json_fragments.isMember(fragment.Name())) {
    LOG(ERROR) << "Could not find a fragment called " << fragment.Name();
    return false;
  }
  return fragment.Deserialize(json_fragments[fragment.Name()]);
}
bool CuttlefishConfig::SaveFragment(const ConfigFragment& fragment) {
  Json::Value& json_fragments = (*dictionary_)[kFragments];
  if (json_fragments.isMember(fragment.Name())) {
    LOG(ERROR) << "Already have a fragment called " << fragment.Name();
    return false;
  }
  json_fragments[fragment.Name()] = fragment.Serialize();
  return true;
}

static constexpr char kRootDir[] = "root_dir";
std::string CuttlefishConfig::root_dir() const {
  return (*dictionary_)[kRootDir].asString();
}
void CuttlefishConfig::set_root_dir(const std::string& root_dir) {
  (*dictionary_)[kRootDir] = root_dir;
}

static constexpr char kVmManager[] = "vm_manager";
std::string CuttlefishConfig::vm_manager() const {
  return (*dictionary_)[kVmManager].asString();
}
void CuttlefishConfig::set_vm_manager(const std::string& name) {
  (*dictionary_)[kVmManager] = name;
}

static constexpr char kApVmManager[] = "ap_vm_manager";
std::string CuttlefishConfig::ap_vm_manager() const {
  return (*dictionary_)[kApVmManager].asString();
}
void CuttlefishConfig::set_ap_vm_manager(const std::string& name) {
  (*dictionary_)[kApVmManager] = name;
}

static SecureHal StringToSecureHal(std::string mode) {
  std::transform(mode.begin(), mode.end(), mode.begin(), ::tolower);
  std::unordered_map<std::string, SecureHal> mapping = {
      {"keymint", SecureHal::HostKeymintSecure},
      {"host_secure_keymint", SecureHal::HostKeymintSecure},
      {"host_keymint_secure", SecureHal::HostKeymintSecure},
      {"guest_insecure_keymint", SecureHal::GuestKeymintInsecure},
      {"guest_keymint_insecure", SecureHal::GuestKeymintInsecure},
      {"gatekeeper", SecureHal::HostGatekeeperSecure},
      {"host_gatekeeper_secure", SecureHal::HostGatekeeperSecure},
      {"host_secure_gatekeeper", SecureHal::HostGatekeeperSecure},
      {"host_gatekeeper_insecure", SecureHal::HostGatekeeperInsecure},
      {"host_insecure_gatekeeper", SecureHal::HostGatekeeperInsecure},
      {"oemlock", SecureHal::HostOemlockSecure},
      {"host_oemlock_secure", SecureHal::HostOemlockSecure},
      {"host_secure_oemlock", SecureHal::HostOemlockSecure},
  };
  auto it = mapping.find(mode);
  return it == mapping.end() ? SecureHal::Unknown : it->second;
}

static constexpr char kSecureHals[] = "secure_hals";
std::set<SecureHal> CuttlefishConfig::secure_hals() const {
  std::set<SecureHal> args_set;
  for (auto& hal : (*dictionary_)[kSecureHals]) {
    args_set.insert(StringToSecureHal(hal.asString()));
  }
  return args_set;
}
void CuttlefishConfig::set_secure_hals(const std::set<std::string>& hals) {
  Json::Value hals_json_obj(Json::arrayValue);
  for (const auto& hal : hals) {
    hals_json_obj.append(hal);
  }
  (*dictionary_)[kSecureHals] = hals_json_obj;
}

static constexpr char kCrosvmBinary[] = "crosvm_binary";
std::string CuttlefishConfig::crosvm_binary() const {
  return (*dictionary_)[kCrosvmBinary].asString();
}
void CuttlefishConfig::set_crosvm_binary(const std::string& crosvm_binary) {
  (*dictionary_)[kCrosvmBinary] = crosvm_binary;
}

bool CuttlefishConfig::IsCrosvm() const { return vm_manager() == "crosvm"; }

static constexpr char kGem5DebugFlags[] = "gem5_debug_flags";
std::string CuttlefishConfig::gem5_debug_flags() const {
  return (*dictionary_)[kGem5DebugFlags].asString();
}
void CuttlefishConfig::set_gem5_debug_flags(const std::string& gem5_debug_flags) {
  (*dictionary_)[kGem5DebugFlags] = gem5_debug_flags;
}

static constexpr char kWebRTCCertsDir[] = "webrtc_certs_dir";
void CuttlefishConfig::set_webrtc_certs_dir(const std::string& certs_dir) {
  (*dictionary_)[kWebRTCCertsDir] = certs_dir;
}
std::string CuttlefishConfig::webrtc_certs_dir() const {
  return (*dictionary_)[kWebRTCCertsDir].asString();
}

static constexpr char kSigServerPort[] = "webrtc_sig_server_port";
void CuttlefishConfig::set_sig_server_port(int port) {
  (*dictionary_)[kSigServerPort] = port;
}
int CuttlefishConfig::sig_server_port() const {
  return (*dictionary_)[kSigServerPort].asInt();
}

static constexpr char kSigServerAddress[] = "webrtc_sig_server_addr";
void CuttlefishConfig::set_sig_server_address(const std::string& addr) {
  (*dictionary_)[kSigServerAddress] = addr;
}
std::string CuttlefishConfig::sig_server_address() const {
  return (*dictionary_)[kSigServerAddress].asString();
}

static constexpr char kSigServerPath[] = "webrtc_sig_server_path";
void CuttlefishConfig::set_sig_server_path(const std::string& path) {
  // Don't use SetPath here, it's a URL path not a file system path
  (*dictionary_)[kSigServerPath] = path;
}
std::string CuttlefishConfig::sig_server_path() const {
  return (*dictionary_)[kSigServerPath].asString();
}

static constexpr char kSigServerSecure[] = "webrtc_sig_server_secure";
void CuttlefishConfig::set_sig_server_secure(bool secure) {
  (*dictionary_)[kSigServerSecure] = secure;
}
bool CuttlefishConfig::sig_server_secure() const {
  return (*dictionary_)[kSigServerSecure].asBool();
}

static constexpr char kSigServerStrict[] = "webrtc_sig_server_strict";
void CuttlefishConfig::set_sig_server_strict(bool strict) {
  (*dictionary_)[kSigServerStrict] = strict;
}
bool CuttlefishConfig::sig_server_strict() const {
  return (*dictionary_)[kSigServerStrict].asBool();
}

static constexpr char kHostToolsVersion[] = "host_tools_version";
void CuttlefishConfig::set_host_tools_version(
    const std::map<std::string, uint32_t>& versions) {
  Json::Value json(Json::objectValue);
  for (const auto& [key, value] : versions) {
    json[key] = value;
  }
  (*dictionary_)[kHostToolsVersion] = json;
}
std::map<std::string, uint32_t> CuttlefishConfig::host_tools_version() const {
  if (!dictionary_->isMember(kHostToolsVersion)) {
    return {};
  }
  std::map<std::string, uint32_t> versions;
  const auto& elem = (*dictionary_)[kHostToolsVersion];
  for (auto it = elem.begin(); it != elem.end(); it++) {
    versions[it.key().asString()] = it->asUInt();
  }
  return versions;
}

static constexpr char kEnableHostUwb[] = "enable_host_uwb";
void CuttlefishConfig::set_enable_host_uwb(bool enable_host_uwb) {
  (*dictionary_)[kEnableHostUwb] = enable_host_uwb;
}
bool CuttlefishConfig::enable_host_uwb() const {
  return (*dictionary_)[kEnableHostUwb].asBool();
}

static constexpr char kEnableHostUwbConnector[] = "enable_host_uwb_connector";
void CuttlefishConfig::set_enable_host_uwb_connector(bool enable_host_uwb) {
  (*dictionary_)[kEnableHostUwbConnector] = enable_host_uwb;
}
bool CuttlefishConfig::enable_host_uwb_connector() const {
  return (*dictionary_)[kEnableHostUwbConnector].asBool();
}

static constexpr char kPicaUciPort[] = "pica_uci_port";
int CuttlefishConfig::pica_uci_port() const {
  return (*dictionary_)[kPicaUciPort].asInt();
}
void CuttlefishConfig::set_pica_uci_port(int pica_uci_port) {
  (*dictionary_)[kPicaUciPort] = pica_uci_port;
}

static constexpr char kEnableHostBluetooth[] = "enable_host_bluetooth";
void CuttlefishConfig::set_enable_host_bluetooth(bool enable_host_bluetooth) {
  (*dictionary_)[kEnableHostBluetooth] = enable_host_bluetooth;
}
bool CuttlefishConfig::enable_host_bluetooth() const {
  return (*dictionary_)[kEnableHostBluetooth].asBool();
}

static constexpr char kEnableHostBluetoothConnector[] =
    "enable_host_bluetooth_connector";
void CuttlefishConfig::set_enable_host_bluetooth_connector(bool enable_host_bluetooth) {
  (*dictionary_)[kEnableHostBluetoothConnector] = enable_host_bluetooth;
}
bool CuttlefishConfig::enable_host_bluetooth_connector() const {
  return (*dictionary_)[kEnableHostBluetoothConnector].asBool();
}

static constexpr char kEnableAutomotiveProxy[] = "enable_automotive_proxy";
void CuttlefishConfig::set_enable_automotive_proxy(
    bool enable_automotive_proxy) {
  (*dictionary_)[kEnableAutomotiveProxy] = enable_automotive_proxy;
}
bool CuttlefishConfig::enable_automotive_proxy() const {
  return (*dictionary_)[kEnableAutomotiveProxy].asBool();
}

static constexpr char kEnableHostNfc[] = "enable_host_nfc";
void CuttlefishConfig::set_enable_host_nfc(bool enable_host_nfc) {
  (*dictionary_)[kEnableHostNfc] = enable_host_nfc;
}
bool CuttlefishConfig::enable_host_nfc() const {
  return (*dictionary_)[kEnableHostNfc].asBool();
}

static constexpr char kEnableHostNfcConnector[] = "enable_host_nfc_connector";
void CuttlefishConfig::set_enable_host_nfc_connector(bool enable_host_nfc) {
  (*dictionary_)[kEnableHostNfcConnector] = enable_host_nfc;
}
bool CuttlefishConfig::enable_host_nfc_connector() const {
  return (*dictionary_)[kEnableHostNfcConnector].asBool();
}

static constexpr char kCasimirInstanceNum[] = "casimir_instance_num";
void CuttlefishConfig::set_casimir_instance_num(int casimir_instance_num) {
  (*dictionary_)[kCasimirInstanceNum] = casimir_instance_num;
}
int CuttlefishConfig::casimir_instance_num() const {
  return (*dictionary_)[kCasimirInstanceNum].asInt();
}

static constexpr char kCasimirArgs[] = "casimir_args";
void CuttlefishConfig::set_casimir_args(const std::string& casimir_args) {
  Json::Value args_json_obj(Json::arrayValue);
  for (const auto& arg : android::base::Split(casimir_args, " ")) {
    if (!arg.empty()) {
      args_json_obj.append(arg);
    }
  }
  (*dictionary_)[kCasimirArgs] = args_json_obj;
}
std::vector<std::string> CuttlefishConfig::casimir_args() const {
  std::vector<std::string> casimir_args;
  for (const Json::Value& arg : (*dictionary_)[kCasimirArgs]) {
    casimir_args.push_back(arg.asString());
  }
  return casimir_args;
}

static constexpr char kCasimirNciPort[] = "casimir_nci_port";
void CuttlefishConfig::set_casimir_nci_port(int port) {
  (*dictionary_)[kCasimirNciPort] = port;
}
int CuttlefishConfig::casimir_nci_port() const {
  return (*dictionary_)[kCasimirNciPort].asInt();
}

static constexpr char kCasimirRfPort[] = "casimir_rf_port";
void CuttlefishConfig::set_casimir_rf_port(int port) {
  (*dictionary_)[kCasimirRfPort] = port;
}
int CuttlefishConfig::casimir_rf_port() const {
  return (*dictionary_)[kCasimirRfPort].asInt();
}

static constexpr char kEnableWifi[] = "enable_wifi";
void CuttlefishConfig::set_enable_wifi(bool enable_wifi) {
  (*dictionary_)[kEnableWifi] = enable_wifi;
}
bool CuttlefishConfig::enable_wifi() const {
  return (*dictionary_)[kEnableWifi].asBool();
}

static constexpr char kNetsimRadios[] = "netsim_radios";

void CuttlefishConfig::netsim_radio_enable(NetsimRadio flag) {
  if (dictionary_->isMember(kNetsimRadios)) {
    // OR the radio to current set of radios
    (*dictionary_)[kNetsimRadios] = (*dictionary_)[kNetsimRadios].asInt() | flag;
  } else {
    (*dictionary_)[kNetsimRadios] = flag;
  }
}

bool CuttlefishConfig::netsim_radio_enabled(NetsimRadio flag) const {
  return (*dictionary_)[kNetsimRadios].asInt() & flag;
}

static constexpr char kNetsimInstanceNum[] = "netsim_instance_num";
int CuttlefishConfig::netsim_instance_num() const {
  return (*dictionary_)[kNetsimInstanceNum].asInt();
}
void CuttlefishConfig::set_netsim_instance_num(int netsim_instance_num) {
  (*dictionary_)[kNetsimInstanceNum] = netsim_instance_num;
}

static constexpr char kNetsimConnectorInstanceNum[] =
    "netsim_connector_instance_num";
int CuttlefishConfig::netsim_connector_instance_num() const {
  return (*dictionary_)[kNetsimConnectorInstanceNum].asInt();
}
void CuttlefishConfig::set_netsim_connector_instance_num(
    int netsim_instance_num) {
  (*dictionary_)[kNetsimConnectorInstanceNum] = netsim_instance_num;
}

static constexpr char kNetsimArgs[] = "netsim_args";
void CuttlefishConfig::set_netsim_args(const std::string& netsim_args) {
  Json::Value args_json_obj(Json::arrayValue);
  for (const auto& arg : android::base::Tokenize(netsim_args, " ")) {
    args_json_obj.append(arg);
  }
  (*dictionary_)[kNetsimArgs] = args_json_obj;
}
std::vector<std::string> CuttlefishConfig::netsim_args() const {
  std::vector<std::string> netsim_args;
  for (const Json::Value& arg : (*dictionary_)[kNetsimArgs]) {
    netsim_args.push_back(arg.asString());
  }
  return netsim_args;
}

static constexpr char kEnableMetrics[] = "enable_metrics";
void CuttlefishConfig::set_enable_metrics(std::string enable_metrics) {
  (*dictionary_)[kEnableMetrics] =
      static_cast<int>(cuttlefish::CuttlefishConfig::Answer::kUnknown);
  if (!enable_metrics.empty()) {
    switch (enable_metrics.at(0)) {
      case 'y':
      case 'Y':
        (*dictionary_)[kEnableMetrics] =
            static_cast<int>(cuttlefish::CuttlefishConfig::Answer::kYes);
        break;
      case 'n':
      case 'N':
        (*dictionary_)[kEnableMetrics] =
            static_cast<int>(cuttlefish::CuttlefishConfig::Answer::kNo);
        break;
    }
  }
}

// validate the casting and conversion from json configs to
// CuttlefishConfig::Answer class
bool IsValidMetricsConfigs(int value) {
  return value == static_cast<int>(CuttlefishConfig::Answer::kUnknown) ||
         value == static_cast<int>(CuttlefishConfig::Answer::kNo) ||
         value == static_cast<int>(CuttlefishConfig::Answer::kYes);
}

CuttlefishConfig::Answer CuttlefishConfig::enable_metrics() const {
  int value = (*dictionary_)[kEnableMetrics].asInt();
  if (!IsValidMetricsConfigs(value)) {
    LOG(ERROR) << "Invalid integer value for Answer enum";
    return static_cast<Answer>(CuttlefishConfig::Answer::kUnknown);
  }
  return static_cast<Answer>(value);
}

static constexpr char kMetricsBinary[] = "metrics_binary";
void CuttlefishConfig::set_metrics_binary(const std::string& metrics_binary) {
  (*dictionary_)[kMetricsBinary] = metrics_binary;
}
std::string CuttlefishConfig::metrics_binary() const {
  return (*dictionary_)[kMetricsBinary].asString();
}

static constexpr char kExtraKernelCmdline[] = "extra_kernel_cmdline";
void CuttlefishConfig::set_extra_kernel_cmdline(
    const std::string& extra_cmdline) {
  Json::Value args_json_obj(Json::arrayValue);
  for (const auto& arg : android::base::Split(extra_cmdline, " ")) {
    args_json_obj.append(arg);
  }
  (*dictionary_)[kExtraKernelCmdline] = args_json_obj;
}
std::vector<std::string> CuttlefishConfig::extra_kernel_cmdline() const {
  std::vector<std::string> cmdline;
  for (const Json::Value& arg : (*dictionary_)[kExtraKernelCmdline]) {
    cmdline.push_back(arg.asString());
  }
  return cmdline;
}

static constexpr char kVirtioMac80211Hwsim[] = "virtio_mac80211_hwsim";
void CuttlefishConfig::set_virtio_mac80211_hwsim(bool virtio_mac80211_hwsim) {
  (*dictionary_)[kVirtioMac80211Hwsim] = virtio_mac80211_hwsim;
}
bool CuttlefishConfig::virtio_mac80211_hwsim() const {
  return (*dictionary_)[kVirtioMac80211Hwsim].asBool();
}

static constexpr char kApRootfsImage[] = "ap_rootfs_image";
std::string CuttlefishConfig::ap_rootfs_image() const {
  return (*dictionary_)[kApRootfsImage].asString();
}
void CuttlefishConfig::set_ap_rootfs_image(const std::string& ap_rootfs_image) {
  (*dictionary_)[kApRootfsImage] = ap_rootfs_image;
}

static constexpr char kApKernelImage[] = "ap_kernel_image";
std::string CuttlefishConfig::ap_kernel_image() const {
  return (*dictionary_)[kApKernelImage].asString();
}
void CuttlefishConfig::set_ap_kernel_image(const std::string& ap_kernel_image) {
  (*dictionary_)[kApKernelImage] = ap_kernel_image;
}

static constexpr char kRootcanalArgs[] = "rootcanal_args";
void CuttlefishConfig::set_rootcanal_args(const std::string& rootcanal_args) {
  Json::Value args_json_obj(Json::arrayValue);
  for (const auto& arg : android::base::Split(rootcanal_args, " ")) {
    args_json_obj.append(arg);
  }
  (*dictionary_)[kRootcanalArgs] = args_json_obj;
}
std::vector<std::string> CuttlefishConfig::rootcanal_args() const {
  std::vector<std::string> rootcanal_args;
  for (const Json::Value& arg : (*dictionary_)[kRootcanalArgs]) {
    rootcanal_args.push_back(arg.asString());
  }
  return rootcanal_args;
}

static constexpr char kRootcanalHciPort[] = "rootcanal_hci_port";
int CuttlefishConfig::rootcanal_hci_port() const {
  return (*dictionary_)[kRootcanalHciPort].asInt();
}
void CuttlefishConfig::set_rootcanal_hci_port(int rootcanal_hci_port) {
  (*dictionary_)[kRootcanalHciPort] = rootcanal_hci_port;
}

static constexpr char kRootcanalLinkPort[] = "rootcanal_link_port";
int CuttlefishConfig::rootcanal_link_port() const {
  return (*dictionary_)[kRootcanalLinkPort].asInt();
}
void CuttlefishConfig::set_rootcanal_link_port(int rootcanal_link_port) {
  (*dictionary_)[kRootcanalLinkPort] = rootcanal_link_port;
}

static constexpr char kRootcanalLinkBlePort[] = "rootcanal_link_ble_port";
int CuttlefishConfig::rootcanal_link_ble_port() const {
  return (*dictionary_)[kRootcanalLinkBlePort].asInt();
}
void CuttlefishConfig::set_rootcanal_link_ble_port(
    int rootcanal_link_ble_port) {
  (*dictionary_)[kRootcanalLinkBlePort] = rootcanal_link_ble_port;
}

static constexpr char kRootcanalTestPort[] = "rootcanal_test_port";
int CuttlefishConfig::rootcanal_test_port() const {
  return (*dictionary_)[kRootcanalTestPort].asInt();
}
void CuttlefishConfig::set_rootcanal_test_port(int rootcanal_test_port) {
  (*dictionary_)[kRootcanalTestPort] = rootcanal_test_port;
}

static constexpr char kSnapshotPath[] = "snapshot_path";
std::string CuttlefishConfig::snapshot_path() const {
  return (*dictionary_)[kSnapshotPath].asString();
}
void CuttlefishConfig::set_snapshot_path(const std::string& snapshot_path) {
  (*dictionary_)[kSnapshotPath] = snapshot_path;
}

static constexpr char kStracedExecutables[] = "straced_host_executables";
void CuttlefishConfig::set_straced_host_executables(
    const std::set<std::string>& straced_host_executables) {
  Json::Value args_json_obj(Json::arrayValue);
  for (const auto& arg : straced_host_executables) {
    args_json_obj.append(arg);
  }
  (*dictionary_)[kStracedExecutables] = args_json_obj;
}
std::set<std::string> CuttlefishConfig::straced_host_executables() const {
  std::set<std::string> straced_host_executables;
  for (const Json::Value& arg : (*dictionary_)[kStracedExecutables]) {
    straced_host_executables.insert(arg.asString());
  }
  return straced_host_executables;
}

static constexpr char kHostSandbox[] = "host_sandbox";
bool CuttlefishConfig::host_sandbox() const {
  return (*dictionary_)[kHostSandbox].asBool();
}
void CuttlefishConfig::set_host_sandbox(bool host_sandbox) {
  (*dictionary_)[kHostSandbox] = host_sandbox;
}

/*static*/ CuttlefishConfig* CuttlefishConfig::BuildConfigImpl(
    const std::string& path) {
  auto ret = new CuttlefishConfig();
  if (ret) {
    auto loaded = ret->LoadFromFile(path.c_str());
    if (!loaded) {
      delete ret;
      return nullptr;
    }
  }
  return ret;
}

/*static*/ std::unique_ptr<const CuttlefishConfig>
CuttlefishConfig::GetFromFile(const std::string& path) {
  return std::unique_ptr<const CuttlefishConfig>(BuildConfigImpl(path));
}

// Creates the (initially empty) config object and populates it with values from
// the config file if the CUTTLEFISH_CONFIG_FILE env variable is present.
// Returns nullptr if there was an error loading from file
/*static*/ const CuttlefishConfig* CuttlefishConfig::Get() {
  auto config_file_path =
      StringFromEnv(kCuttlefishConfigEnvVarName, GetGlobalConfigFileLink());
  static std::shared_ptr<CuttlefishConfig> config(
      BuildConfigImpl(config_file_path));
  return config.get();
}

/*static*/ bool CuttlefishConfig::ConfigExists() {
  auto config_file_path = StringFromEnv(kCuttlefishConfigEnvVarName,
                                        GetGlobalConfigFileLink());
  auto real_file_path = AbsolutePath(config_file_path.c_str());
  return FileExists(real_file_path);
}

CuttlefishConfig::CuttlefishConfig() : dictionary_(new Json::Value()) {}
// Can't use '= default' on the header because the compiler complains of
// Json::Value being an incomplete type
CuttlefishConfig::~CuttlefishConfig() = default;

CuttlefishConfig::CuttlefishConfig(CuttlefishConfig&&) = default;
CuttlefishConfig& CuttlefishConfig::operator=(CuttlefishConfig&&) = default;

bool CuttlefishConfig::LoadFromFile(const char* file) {
  auto real_file_path = AbsolutePath(file);
  if (real_file_path.empty()) {
    LOG(ERROR) << "Could not get real path for file " << file;
    return false;
  }
  Json::CharReaderBuilder builder;
  std::ifstream ifs(real_file_path);
  std::string errorMessage;
  if (!Json::parseFromStream(builder, ifs, dictionary_.get(), &errorMessage)) {
    LOG(ERROR) << "Could not read config file " << file << ": " << errorMessage;
    return false;
  }
  return true;
}
bool CuttlefishConfig::SaveToFile(const std::string& file) const {
  std::ofstream ofs(file);
  if (!ofs.is_open()) {
    LOG(ERROR) << "Unable to write to file " << file;
    return false;
  }
  ofs << *dictionary_;
  return !ofs.fail();
}

std::string CuttlefishConfig::instances_dir() const {
  return AbsolutePath(root_dir() + "/instances");
}

std::string CuttlefishConfig::InstancesPath(
    const std::string& file_name) const {
  return AbsolutePath(instances_dir() + "/" + file_name);
}

std::string CuttlefishConfig::assembly_dir() const {
  return AbsolutePath(root_dir() + "/assembly");
}

std::string CuttlefishConfig::AssemblyPath(
    const std::string& file_name) const {
  return AbsolutePath(assembly_dir() + "/" + file_name);
}

std::string CuttlefishConfig::instances_uds_dir() const {
  // Try to use /tmp/cf_avd_{uid}/ for UDS directory.
  // If it fails, use HOME directory(legacy) instead.

  auto defaultPath = AbsolutePath("/tmp/cf_avd_" + std::to_string(getuid()));

  if (!DirectoryExists(defaultPath) ||
      CanAccess(defaultPath, R_OK | W_OK | X_OK)) {
    return defaultPath;
  }

  return instances_dir();
}

std::string CuttlefishConfig::InstancesUdsPath(
    const std::string& file_name) const {
  return AbsolutePath(instances_uds_dir() + "/" + file_name);
}

std::string CuttlefishConfig::environments_dir() const {
  return AbsolutePath(root_dir() + "/environments");
}

std::string CuttlefishConfig::EnvironmentsPath(
    const std::string& file_name) const {
  return AbsolutePath(environments_dir() + "/" + file_name);
}

std::string CuttlefishConfig::environments_uds_dir() const {
  // Try to use /tmp/cf_env_{uid}/ for UDS directory.
  // If it fails, use HOME directory instead.

  auto defaultPath = AbsolutePath("/tmp/cf_env_" + std::to_string(getuid()));

  if (!DirectoryExists(defaultPath) ||
      CanAccess(defaultPath, R_OK | W_OK | X_OK)) {
    return defaultPath;
  }

  return environments_dir();
}

std::string CuttlefishConfig::EnvironmentsUdsPath(
    const std::string& file_name) const {
  return AbsolutePath(environments_uds_dir() + "/" + file_name);
}

CuttlefishConfig::MutableInstanceSpecific CuttlefishConfig::ForInstance(int num) {
  return MutableInstanceSpecific(this, std::to_string(num));
}

CuttlefishConfig::InstanceSpecific CuttlefishConfig::ForInstance(int num) const {
  return InstanceSpecific(this, std::to_string(num));
}

CuttlefishConfig::InstanceSpecific CuttlefishConfig::ForInstanceName(
    const std::string& name) const {
  return ForInstance(InstanceFromString(name));
}

CuttlefishConfig::InstanceSpecific CuttlefishConfig::ForDefaultInstance() const {
  return ForInstance(GetInstance());
}

std::vector<CuttlefishConfig::InstanceSpecific> CuttlefishConfig::Instances() const {
  const auto& json = (*dictionary_)[kInstances];
  std::vector<CuttlefishConfig::InstanceSpecific> instances;
  for (const auto& name : json.getMemberNames()) {
    instances.push_back(CuttlefishConfig::InstanceSpecific(this, name));
  }
  return instances;
}

std::vector<std::string> CuttlefishConfig::instance_dirs() const {
  std::vector<std::string> result;
  for (const auto& instance : Instances()) {
    result.push_back(instance.instance_dir());
    result.push_back(instance.instance_uds_dir());
  }
  return result;
}

static constexpr char kInstanceNames[] = "instance_names";
void CuttlefishConfig::set_instance_names(
    const std::vector<std::string>& instance_names) {
  Json::Value args_json_obj(Json::arrayValue);
  for (const auto& name : instance_names) {
    args_json_obj.append(name);
  }
  (*dictionary_)[kInstanceNames] = args_json_obj;
}
std::vector<std::string> CuttlefishConfig::instance_names() const {
  // NOTE: The structure of this field needs to remain stable, since
  // cvd_server may call this on config JSON files from various builds.
  //
  // This info is duplicated into its own field here so it is simpler
  // to keep stable, rather than parsing from Instances()::instance_name.
  //
  // Any non-stable changes must be accompanied by an uprev to the
  // cvd_server major version.
  std::vector<std::string> names;
  for (const Json::Value& name : (*dictionary_)[kInstanceNames]) {
    names.push_back(name.asString());
  }
  return names;
}

CuttlefishConfig::MutableEnvironmentSpecific CuttlefishConfig::ForEnvironment(
    const std::string& envName) {
  return MutableEnvironmentSpecific(this, envName);
}

CuttlefishConfig::EnvironmentSpecific CuttlefishConfig::ForEnvironment(
    const std::string& envName) const {
  return EnvironmentSpecific(this, envName);
}

CuttlefishConfig::MutableEnvironmentSpecific
CuttlefishConfig::ForDefaultEnvironment() {
  return MutableEnvironmentSpecific(this,
                                    ForDefaultInstance().environment_name());
}

CuttlefishConfig::EnvironmentSpecific CuttlefishConfig::ForDefaultEnvironment()
    const {
  return EnvironmentSpecific(this, ForDefaultInstance().environment_name());
}

std::vector<std::string> CuttlefishConfig::environment_dirs() const {
  auto environment = ForDefaultEnvironment();

  std::vector<std::string> result;

  result.push_back(environment.environment_dir());
  result.push_back(environment.environment_uds_dir());

  return result;
}

}  // namespace cuttlefish
