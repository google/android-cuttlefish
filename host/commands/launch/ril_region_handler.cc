/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <stdio.h>
#include <string.h>

#include <memory>
#include <string>

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include "common/vsoc/lib/ril_region_view.h"
#include "host/commands/launch/pre_launch_initializers.h"
#include "host/libs/config/host_config.h"

DECLARE_string(hypervisor_uri);
DECLARE_string(mobile_interface);

namespace {

int number_of_ones(int val) {
  int ret = 0;
  while (val) {
    ret += val % 2;
    val >>= 1;
  }
  return ret;
}

class NetConfig {
 public:
  uint32_t ril_prefixlen = -1;
  std::string ril_ipaddr;
  std::string ril_gateway;
  std::string ril_dns = "8.8.8.8";
  std::string ril_broadcast;

  bool ObtainConfig(const std::string& interface) {
    bool ret = ParseLibvirtXml(interface) && GetBroadcastAddr(interface);
    LOG(INFO) << "Network config:";
    LOG(INFO) << "ipaddr = " << ril_ipaddr;
    LOG(INFO) << "gateway = " << ril_gateway;
    LOG(INFO) << "dns = " << ril_dns;
    LOG(INFO) << "broadcast = " << ril_broadcast;
    LOG(INFO) << "prefix length = " << ril_prefixlen;
    return ret;
  }

 private:
  bool GetBroadcastAddr(const std::string& interface) {
    struct ifaddrs *ifap{}, *ifa{};
    struct sockaddr_in *sa{};
    char *addr{};
    getifaddrs (&ifap);
    for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
      if (ifa->ifa_addr && ifa->ifa_addr->sa_family==AF_INET) {
        if (strcmp(ifa->ifa_name, interface.c_str())) continue;
        sa = reinterpret_cast<sockaddr_in*>(ifa->ifa_ifu.ifu_broadaddr);
        addr = inet_ntoa(sa->sin_addr);
        this->ril_broadcast = strtok(addr, "\n");
      }
    }

    freeifaddrs(ifap);
    return (this->ril_broadcast.size() > 0);
  }

  bool ParseLibvirtXml(const std::string& interface) {
    std::string net_dump_command =
        "virsh -c " + FLAGS_hypervisor_uri + " net-dumpxml " + interface;
    std::shared_ptr<FILE> net_xml_file(popen(net_dump_command.c_str(), "r"),
                                       pclose);
    if (!net_xml_file) {
      LOG(ERROR) << "Unable to popen virsh...";
      return false;
    }
    std::shared_ptr<xmlDoc> doc(
        xmlReadFd(fileno(net_xml_file.get()), NULL, NULL, 0), xmlFreeDoc);
    if (!doc) {
      LOG(ERROR) << "Unable to parse network xml";
      return false;
    }

    xmlNode* element = xmlDocGetRootElement(doc.get());
    element = element->xmlChildrenNode;
    while (element) {
      if (strcmp(reinterpret_cast<const char*>(element->name), "ip") == 0) {
        return ProcessIpNode(element);
      }
      element = element->next;
    }
    LOG(ERROR) << "ip node not found in network xml spec";
    return false;
  }

  bool ParseIpAttributes(xmlNode* ip_node) {
    // The gateway is the host ip address
    this->ril_gateway = reinterpret_cast<const char*>(
        xmlGetProp(ip_node, reinterpret_cast<const xmlChar*>("address")));

    // The prefix length need to be obtained from the network mask
    auto* netmask = reinterpret_cast<const char*>(
        xmlGetProp(ip_node, reinterpret_cast<const xmlChar*>("netmask")));
    int byte1, byte2, byte3, byte4;
    sscanf(netmask, "%d.%d.%d.%d", &byte1, &byte2, &byte3, &byte4);
    this->ril_prefixlen = 0;
    this->ril_prefixlen += number_of_ones(byte1);
    this->ril_prefixlen += number_of_ones(byte2);
    this->ril_prefixlen += number_of_ones(byte3);
    this->ril_prefixlen += number_of_ones(byte4);
    return true;
  }

  bool ProcessDhcpNode(xmlNode* dhcp_node) {
    xmlNode* child = dhcp_node->xmlChildrenNode;
    while (child) {
      if (strcmp(reinterpret_cast<const char*>(child->name), "range") == 0) {
        this->ril_ipaddr = reinterpret_cast<const char*>(
            xmlGetProp(child, reinterpret_cast<const xmlChar*>("start")));
        return true;
      }
      child = child->next;
    }
    LOG(ERROR) << "range node not found in network xml spec";
    return false;
  }

  bool ProcessIpNode(xmlNode* ip_node) {
    ParseIpAttributes(ip_node);
    xmlNode* child = ip_node->xmlChildrenNode;
    while (child) {
      if (strcmp(reinterpret_cast<const char*>(child->name), "dhcp") == 0) {
        return ProcessDhcpNode(child);
      }
      child = child->next;
    }
    LOG(ERROR) << "dhcp node not found in network xml spec";
    return false;
  }
};
}  // namespace

void InitializeRilRegion() {
  NetConfig netconfig;
  if (!netconfig.ObtainConfig(FLAGS_mobile_interface)) {
    LOG(ERROR) << "Unable to obtain the network configuration";
    return;
  }

  auto region =
      vsoc::ril::RilRegionView::GetInstance(vsoc::GetDomain().c_str());

  if (!region) {
    LOG(ERROR) << "Ril region was not found";
    return;
  }

  auto dest = region->data();

  snprintf(
      dest->ipaddr, sizeof(dest->ipaddr), "%s", netconfig.ril_ipaddr.c_str());
  snprintf(dest->gateway,
           sizeof(dest->gateway),
           "%s",
           netconfig.ril_gateway.c_str());
  snprintf(dest->dns, sizeof(dest->dns), "%s", netconfig.ril_dns.c_str());
  snprintf(dest->broadcast,
           sizeof(dest->broadcast),
           "%s",
           netconfig.ril_broadcast.c_str());
  dest->prefixlen = netconfig.ril_prefixlen;
}
