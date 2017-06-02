/*
 * Copyright (C) 2016 The Android Open Source Project
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
#include "guest/gce_init/environment_setup.h"

#include <functional>
#include <memory>

namespace avd {
namespace {

// We need a network namespace aware iw. Use the backport on branches before N.
#define IW_EXECUTABLE "iw"

// Name of the metadata proxy socket name.
const char kProxySocketName[] = "gce_metadata";

const char* kAndroidNsCommandsCommon[] = {
  "ifconfig wlan0 mtu 1460",
  "ifconfig wlan_ap mtu 1460",
  IW_EXECUTABLE " dev wlan0 set bitrates legacy-2.4 48 54",
  IW_EXECUTABLE " dev wlan_ap set bitrates legacy-2.4 48 54",
  "ifconfig internal0 up mtu 1460",
  "ifconfig internal0 192.168.255.2",

  // Reparent the phy/wlan interface to outer namespace.
  IW_EXECUTABLE " phy phy0 set netns $(</var/run/netns/outer.process)",

  // Enable static route to metadata server through internal interface.
  // This allows us to connect to metadata server when android enters
  // airplane mode, or all network interfaces are down.
  "ip route add 169.254.169.254/32 via 192.168.255.1 dev internal0",

  NULL
};

const char* kAndroidNsCommandsMobile[] = {
  "ip link set rmnet0 up mtu 1460",
  "ip addr add 192.168.1.10/24 dev rmnet0",
  "ip route add default via 192.168.1.1 dev rmnet0",
  NULL
};

const char* kAndroidNsCommandsPortFwd[] = {
  "iptables -t nat -A PREROUTING -p tcp -i internal0 "
      "--dport 6444 -j DNAT --to-destination 127.0.0.1:6444",
  "iptables -A FORWARD -p tcp -d 127.0.0.1 "
      "--dport 6444 -m state --state NEW,ESTABLISHED,RELATED -j ACCEPT",
  "iptables -t nat -A PREROUTING -p tcp -i internal0 "
      "--dport 5555 -j DNAT --to-destination 127.0.0.01:5555",
  "iptables -A FORWARD -p tcp -d 127.0.0.1 "
      "--dport 5555 -m state --state NEW,ESTABLISHED,RELATED -j ACCEPT",
  NULL
};

const char* kOuterNsCommandsCommon[] = {
  // Start loopback interface.
  "ifconfig lo 127.0.0.1",

  // Bring up and configure android1 interface.
  // This enables communication with avd services when android enters
  // airplane mode.
  "ifconfig android1 up mtu 1460",
  "ifconfig android1 192.168.255.1",

  // Executables in the ramdisk are only runnable by root and it's group.
  // dhcpcd insists on running as a different user, so chmod the script to
  // make it execuable.
  "chmod 0555 /",
  "chmod 0555 /bin/gce_init_dhcp_hook",
  "touch /var/run/eth0.dhcp.env",
  "chown dhcp /var/run/eth0.dhcp.env",
  "chmod 0644 /var/run/eth0.dhcp.env",

  // Start DHCP client on primary host interface.
  // DHCP will execute in background.
  // A: no ARPing
  // c: run script
  // L: no bonjour
  // d: show debug output
  // p: persist configuration
  "dhcpcd-6.8.2 -ALdp -c /bin/gce_init_dhcp_hook host_eth0",

  // Fix the interface mtu
  "( . /var/run/eth0.dhcp.env ; ifconfig host_eth0 mtu ${new_interface_mtu})",

  // Start HostAPD.
  "ifconfig wlan_ap up mtu 1460",
  "ifconfig wlan_ap 192.168.2.1",
  "hostapd -B /system/etc/wifi/simulated_hostapd.conf",

  // Set up NAT.
  "echo 1 > /proc/sys/net/ipv4/ip_forward",
  "iptables -t nat -A POSTROUTING -s 192.168.1.0/24 "
      "-o host_eth0 -j MASQUERADE",
  "iptables -t nat -A POSTROUTING -s 192.168.2.0/24 "
      "-o host_eth0 -j MASQUERADE",
  "iptables -t nat -A POSTROUTING -s 192.168.255.0/24 "
      "-o host_eth0 -j MASQUERADE",

  // SSH port forwarding.
  "iptables -t nat -A PREROUTING -p tcp -i host_eth0 "
      "--dport 22 -j DNAT --to-destination 192.168.255.2:22",
  "iptables -A FORWARD -p tcp -d 192.168.255.2 "
      "--dport 22 -m state --state NEW,ESTABLISHED,RELATED -j ACCEPT",

  // Enable masquerading.
  "iptables -t nat -A POSTROUTING -j MASQUERADE",

  // Print network diagnostic details.
  "ip link",
  "ip addr",
  "ip route list",
  "cat /var/run/eth0.dhcp.env",
  NULL
};

const char* kOuterNsCommandsMobile[] = {
  // Bring up and configure android0 interface.
  // Two steps required, otherwise ifconfig complains about link not ready.
  "ifconfig android0 up mtu 1460",
  "ifconfig android0 192.168.1.1",
  NULL
};

const char* kOuterNsCommandsPortFwd[] = {
  // VNC & ADB port forwarding.
  "iptables -t nat -A PREROUTING -p tcp -i host_eth0 "
      "--dport 6444 -j DNAT --to-destination 192.168.255.2:6444",
  "iptables -A FORWARD -p tcp -d 192.168.255.2 "
      "--dport 6444 -m state --state NEW,ESTABLISHED,RELATED -j ACCEPT",
  "iptables -t nat -A PREROUTING -p tcp -i host_eth0 "
      "--dport 5555 -j DNAT --to-destination 192.168.255.2:5555",
  "iptables -A FORWARD -p tcp -d 192.168.255.2 "
      "--dport 5555 -m state --state NEW,ESTABLISHED,RELATED -j ACCEPT",
  NULL
};

}  // namespace

void EnvironmentSetup::CreateMetadataProxy() {
  executor_->Execute(
      NetworkNamespaceManager::kOuterNs,
      [this]() -> int32_t {
        MetadataProxy::New(sys_client_, ns_manager_)->Start(kProxySocketName);
        return 0;
      });
}

void EnvironmentSetup::CreateDhcpServer(
    const std::string& namespace_name, const DhcpServer::Options& options) {
  executor_->Execute(
      namespace_name,
      [options]() -> int32_t {
        DhcpServer::New()->Start(options);
        return 0;
      });
}

bool EnvironmentSetup::ConfigureNetworkCommon() {
  // Rename host eth0 interface to avoid name conflicts.
  // Put the interface in 'outer' namespace.
  std::unique_ptr<NetworkInterface> iface(if_manager_->Open("eth0"));
  iface->set_name("host_eth0").set_network_namespace(
      NetworkNamespaceManager::kOuterNs);
  if_manager_->ApplyChanges(*iface);

  // WLAN0 uses the MAC address recognized by Android as fake.
  // We control this interface - and to make it explicit - give it a name
  // indicating its purpose.
  iface.reset(if_manager_->Open("wlan0"));
  iface->set_name("wlan_ap");
  if_manager_->ApplyChanges(*iface);

  // WLAN1 is reparented to Android, which expects it to have name wlan0.
  // Since in future we may be running more android devices, controlling other
  // wlan# interfaces (which will have to be renamed as wlan0 anyway) this is a
  // desired change.
  iface.reset(if_manager_->Open("wlan1"));
  iface->set_name("wlan0");
  if_manager_->ApplyChanges(*iface);

  // Create veth pair that will be used by AVD services internally.
  if_manager_->CreateVethPair(
      NetworkInterface()
          .set_name("internal0").set_network_namespace(
              NetworkNamespaceManager::kAndroidNs),
      NetworkInterface()
          .set_name("android1").set_network_namespace(
              NetworkNamespaceManager::kOuterNs));

  executor_->Execute(
      NetworkNamespaceManager::kAndroidNs, false, kAndroidNsCommandsCommon);

  executor_->Execute(
      NetworkNamespaceManager::kOuterNs, false, kOuterNsCommandsCommon);

  // Start DHCP server.
  CreateDhcpServer(
      NetworkNamespaceManager::kOuterNs, DhcpServer::Options()
      .set_bind_device("wlan_ap")
      .set_server_address("192.168.2.1")
      .set_gateway_address("192.168.2.1")
      .set_start_ip_address("192.168.2.10")
      .set_end_ip_address("192.168.2.100")
      .set_network_mask("255.255.255.0")
      .set_dns_address("8.8.8.8")
      .set_mtu(1460)
      .set_lease_time(DhcpServer::Options::kLeaseTimeInfinite));

  CreateMetadataProxy();

  return true;
}

bool EnvironmentSetup::ConfigureNetworkMobile() {
  // Create veth pair.
  // These interfaces are used to simulate eth0 interface on Android
  // without risking virtual machine connection loss when the interface
  // is down.
  if (!if_manager_->CreateVethPair(
      NetworkInterface().set_name("rmnet0").set_network_namespace(
          NetworkNamespaceManager::kAndroidNs),
      NetworkInterface().set_name("android0").set_network_namespace(
          NetworkNamespaceManager::kOuterNs)))
    return false;

  executor_->Execute(
      NetworkNamespaceManager::kAndroidNs, false, kAndroidNsCommandsMobile);

  executor_->Execute(
      NetworkNamespaceManager::kOuterNs, false, kOuterNsCommandsMobile);

  return true;
}

bool EnvironmentSetup::CreateNamespaces() {
  ns_manager_->CreateNetworkNamespace(
      NetworkNamespaceManager::kOuterNs, true, false);
  ns_manager_->CreateNetworkNamespace(
      NetworkNamespaceManager::kAndroidNs, false, true);

  return true;
}

bool EnvironmentSetup::ConfigurePortForwarding() {
  executor_->Execute(
      NetworkNamespaceManager::kAndroidNs, false, kAndroidNsCommandsPortFwd);

  executor_->Execute(
      NetworkNamespaceManager::kOuterNs, false, kOuterNsCommandsPortFwd);

  return true;
}

}  // namespace avd
