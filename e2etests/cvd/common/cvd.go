// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package e2etests

import (
	"encoding/json"
	"fmt"
	"log"
	"strings"
)

// CVDInstanceStatusEntry represents a single instance's status in `cvd status --print` JSON output.
type CVDInstanceStatusEntry struct {
	AdbPort        int    `json:"adb_port"`
	AdbSerial      string `json:"adb_serial"`
	AssemblyDir    string `json:"assembly_dir"`
	InstanceDir    string `json:"instance_dir"`
	InstanceName   string `json:"instance_name"`
	Status         string `json:"status"`
	WebAccess      string `json:"web_access"`
	WebRtcDeviceID string `json:"webrtc_device_id"`
}

// ParseCVDStatusJSON parses the JSON output returned by `cvd status --print`.
// It handles both single-instance array `[...]` and group object `{"instances": [...]}` formats.
func ParseCVDStatusJSON(output string) ([]CVDInstanceStatusEntry, error) {
	startArray := strings.Index(output, "[")
	startObj := strings.Index(output, "{")

	if startArray == -1 && startObj == -1 {
		return nil, fmt.Errorf("no json found in cvd status output: %s", output)
	}

	if startArray != -1 && (startObj == -1 || startArray < startObj) {
		endArray := strings.LastIndex(output, "]")
		if endArray == -1 || endArray < startArray {
			return nil, fmt.Errorf("malformed json array in cvd status output: %s", output)
		}
		jsonStr := strings.TrimSpace(output[startArray : endArray+1])
		var entries []CVDInstanceStatusEntry
		if err := json.Unmarshal([]byte(jsonStr), &entries); err != nil {
			return nil, fmt.Errorf("failed to parse cvd status array %q: %w", jsonStr, err)
		}
		return entries, nil
	}

	endObj := strings.LastIndex(output, "}")
	if endObj == -1 || endObj < startObj {
		return nil, fmt.Errorf("malformed json object in cvd status output: %s", output)
	}
	jsonStr := strings.TrimSpace(output[startObj : endObj+1])
	var group struct {
		Instances *[]CVDInstanceStatusEntry `json:"instances"`
	}
	if err := json.Unmarshal([]byte(jsonStr), &group); err == nil && group.Instances != nil {
		return *group.Instances, nil
	}
	var single CVDInstanceStatusEntry
	if err := json.Unmarshal([]byte(jsonStr), &single); err == nil && (single.InstanceName != "" || single.Status != "") {
		return []CVDInstanceStatusEntry{single}, nil
	}
	return nil, fmt.Errorf("failed to parse cvd status object %q", jsonStr)
}

// CVDStop performs `cvd stop <args>`.
func CVDStop(tc *TestContext, args ...string) error {
	stopCmd := append([]string{"stop"}, args...)
	if _, err := tc.RunCVD(stopCmd...); err != nil {
		log.Printf("Failed to stop instance(s): %v", err)
		return err
	}
	return nil
}

// CVDStart performs `cvd start <args>`.
func CVDStart(tc *TestContext, args ...string) error {
	startCmd := append([]string{"start"}, args...)
	if _, err := tc.RunCVD(startCmd...); err != nil {
		log.Printf("Failed to start instance(s): %v", err)
		return err
	}
	return nil
}

// CVDRestart performs `cvd restart <args>`.
func CVDRestart(tc *TestContext, args ...string) error {
	restartCmd := append([]string{"restart"}, args...)
	if _, err := tc.RunCVD(restartCmd...); err != nil {
		log.Printf("Failed to restart instance(s): %v", err)
		return err
	}
	return nil
}

// CVDStatus performs `cvd status --print <args>` and parses the status JSON.
func CVDStatus(tc *TestContext, args ...string) ([]CVDInstanceStatusEntry, error) {
	statusCmd := append([]string{"status", "--print"}, args...)
	out, err := tc.RunCVD(statusCmd...)
	if err != nil {
		return nil, fmt.Errorf("cvd status failed: %w", err)
	}
	return ParseCVDStatusJSON(out.Stdout)
}

// CVDInstanceStop performs `cvd --instance_name=<instanceName> stop <args>`.
func CVDInstanceStop(tc *TestContext, instanceName string, args ...string) error {
	cmd := []string{fmt.Sprintf("--instance_name=%s", instanceName), "stop"}
	cmd = append(cmd, args...)
	if _, err := tc.RunCVD(cmd...); err != nil {
		log.Printf("Failed to stop instance %s: %v", instanceName, err)
		return err
	}
	return nil
}

// CVDInstanceStart performs `cvd --instance_name=<instanceName> start <args>`.
func CVDInstanceStart(tc *TestContext, instanceName string, args ...string) error {
	cmd := []string{fmt.Sprintf("--instance_name=%s", instanceName), "start"}
	cmd = append(cmd, args...)
	if _, err := tc.RunCVD(cmd...); err != nil {
		log.Printf("Failed to start instance %s: %v", instanceName, err)
		return err
	}
	return nil
}

// CVDInstanceRestart performs `cvd --instance_name=<instanceName> restart <args>`.
func CVDInstanceRestart(tc *TestContext, instanceName string, args ...string) error {
	cmd := []string{fmt.Sprintf("--instance_name=%s", instanceName), "restart"}
	cmd = append(cmd, args...)
	if _, err := tc.RunCVD(cmd...); err != nil {
		log.Printf("Failed to restart instance %s: %v", instanceName, err)
		return err
	}
	return nil
}

// CVDInstanceStatus performs `cvd --instance_name=<instanceName> status --print <args>` and parses the status JSON.
func CVDInstanceStatus(tc *TestContext, instanceName string, args ...string) ([]CVDInstanceStatusEntry, error) {
	cmd := []string{fmt.Sprintf("--instance_name=%s", instanceName), "status", "--print"}
	cmd = append(cmd, args...)
	out, err := tc.RunCVD(cmd...)
	if err != nil {
		return nil, fmt.Errorf("cvd status failed for instance %s: %w", instanceName, err)
	}
	return ParseCVDStatusJSON(out.Stdout)
}
