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
	"encoding/json"
	"fmt"
	"os/exec"
)

func ParseAdbPorts(stdout string) ([]int, error) {
	type Instance struct {
		AdbPort int `json:"adb_port"`
	}
	type InstanceGroup struct {
		Instances []Instance `json:"instances"`
	}
	var instanceGroup InstanceGroup
	if err := json.Unmarshal([]byte(stdout), &instanceGroup); err != nil {
		return nil, err
	}
	var ports []int
	for _, instance := range instanceGroup.Instances {
		ports = append(ports, instance.AdbPort)
	}
	return ports, nil
}

func EstablishAdbConnection(ip string, ports ...int) error {
	adbBin, err := exec.LookPath("adb")
	if err != nil {
		return fmt.Errorf("failed to find adb: %w", err)
	}
	if err := exec.Command(adbBin, "start-server").Run(); err != nil {
		return fmt.Errorf("failed to start server: %w", err)
	}
	for _, port := range ports {
		if err := exec.Command(adbBin, "connect", fmt.Sprintf("%s:%d", ip, port)).Run(); err != nil {
			return fmt.Errorf("failed to connect to Cuttlefish device: %w", err)
		}
	}
	return nil
}
