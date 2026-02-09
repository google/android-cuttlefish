// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//	http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package internal

import (
	"fmt"
	"os/exec"

	"github.com/google/android-cuttlefish/frontend/src/libcfcontainer"
)

func ConnectAdb(ccm libcfcontainer.CuttlefishContainerManager, instanceGroup InstanceGroup) error {
	return connectOrDisconnectAdb(ccm, instanceGroup, true)
}

func DisconnectAdb(ccm libcfcontainer.CuttlefishContainerManager, instanceGroup InstanceGroup) error {
	return connectOrDisconnectAdb(ccm, instanceGroup, false)
}

func connectOrDisconnectAdb(ccm libcfcontainer.CuttlefishContainerManager, instanceGroup InstanceGroup, enable bool) error {
	groupNameIpAddrMap, err := Ipv4AddressesByGroupNames(ccm)
	if err != nil {
		return fmt.Errorf("failed to get IPv4 addresses for group names: %w", err)
	}
	ip, exists := groupNameIpAddrMap[instanceGroup.GroupName]
	if !exists {
		return fmt.Errorf("failed to find IPv4 address for group name %q", instanceGroup.GroupName)
	}
	return handleAdbConnection(enable, ip, adbPorts(instanceGroup)...)
}

func adbPorts(instanceGroup InstanceGroup) []int {
	var ports []int
	for _, instance := range instanceGroup.Instances {
		if instance.AdbPort > 0 {
			ports = append(ports, instance.AdbPort)
		}
	}
	return ports
}

func handleAdbConnection(enable bool, ip string, ports ...int) error {
	adbBin, err := exec.LookPath("adb")
	if err != nil {
		return fmt.Errorf("failed to find adb: %w", err)
	}
	if err := exec.Command(adbBin, "start-server").Run(); err != nil {
		return fmt.Errorf("failed to start server: %w", err)
	}
	subcommandMap := map[bool]string{true: "connect", false: "disconnect"}
	for _, port := range ports {
		if err := exec.Command(adbBin, subcommandMap[enable], fmt.Sprintf("%s:%d", ip, port)).Run(); err != nil {
			return fmt.Errorf("failed to connect to Cuttlefish device: %w", err)
		}
	}
	return nil
}
