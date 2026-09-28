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
// It handles single-instance object, multi-instance array `[...]`, and group object `{"instances": [...]}` formats.
func ParseCVDStatusJSON(output string) ([]CVDInstanceStatusEntry, error) {
	// 1. Try group object format: {"instances": [...]}
	startObj := strings.Index(output, "{")
	endObj := strings.LastIndex(output, "}")
	if startObj != -1 && endObj > startObj {
		jsonStr := strings.TrimSpace(output[startObj : endObj+1])
		var group struct {
			Instances *[]CVDInstanceStatusEntry `json:"instances"`
		}
		if err := json.Unmarshal([]byte(jsonStr), &group); err == nil && group.Instances != nil {
			return *group.Instances, nil
		}
	}

	// 2. Try array format: [...]
	// Sift past any potential leading bracketed log prefixes (e.g. "[INFO ...]")
	for idx := strings.Index(output, "["); idx != -1; {
		endArray := strings.LastIndex(output, "]")
		if endArray > idx {
			jsonStr := strings.TrimSpace(output[idx : endArray+1])
			var entries []CVDInstanceStatusEntry
			if err := json.Unmarshal([]byte(jsonStr), &entries); err == nil && len(entries) > 0 {
				return entries, nil
			}
		}
		next := strings.Index(output[idx+1:], "[")
		if next == -1 {
			break
		}
		idx += 1 + next
	}

	// 3. Try single instance object format: {"instance_name": ..., "status": ...}
	if startObj != -1 && endObj > startObj {
		jsonStr := strings.TrimSpace(output[startObj : endObj+1])
		var single CVDInstanceStatusEntry
		if err := json.Unmarshal([]byte(jsonStr), &single); err == nil && (single.InstanceName != "" || single.Status != "") {
			return []CVDInstanceStatusEntry{single}, nil
		}
	}

	return nil, fmt.Errorf("failed to parse cvd status json from output: %s", output)
}

// CVDStop performs `cvd stop <args>`.
func (tc *TestContext) CVDStop(args ...string) error {
	stopCmd := append([]string{"stop"}, args...)
	if _, err := tc.RunCVD(stopCmd...); err != nil {
		log.Printf("Failed to stop instance(s): %v", err)
		return err
	}
	return nil
}

// CVDStart performs `cvd start <args>`.
func (tc *TestContext) CVDStart(args ...string) error {
	startCmd := append([]string{"start"}, args...)
	if _, err := tc.RunCVD(startCmd...); err != nil {
		log.Printf("Failed to start instance(s): %v", err)
		return err
	}
	return nil
}

// CVDRestart performs `cvd restart <args>`.
func (tc *TestContext) CVDRestart(args ...string) error {
	restartCmd := append([]string{"restart"}, args...)
	if _, err := tc.RunCVD(restartCmd...); err != nil {
		log.Printf("Failed to restart instance(s): %v", err)
		return err
	}
	return nil
}

// CVDStatus performs `cvd status --print <args>` and parses the status JSON.
func (tc *TestContext) CVDStatus(args ...string) ([]CVDInstanceStatusEntry, error) {
	statusCmd := append([]string{"status", "--print"}, args...)
	out, err := tc.RunCVD(statusCmd...)
	if err != nil {
		return nil, fmt.Errorf("cvd status failed: %w", err)
	}
	return ParseCVDStatusJSON(out.Stdout)
}

// CVDInstanceStop performs `cvd --instance_name=<instanceName> stop <args>`.
func (tc *TestContext) CVDInstanceStop(instanceName string, args ...string) error {
	cmd := []string{fmt.Sprintf("--instance_name=%s", instanceName), "stop"}
	cmd = append(cmd, args...)
	if _, err := tc.RunCVD(cmd...); err != nil {
		log.Printf("Failed to stop instance %s: %v", instanceName, err)
		return err
	}
	return nil
}

// CVDInstanceStart performs `cvd --instance_name=<instanceName> start <args>`.
func (tc *TestContext) CVDInstanceStart(instanceName string, args ...string) error {
	cmd := []string{fmt.Sprintf("--instance_name=%s", instanceName), "start"}
	cmd = append(cmd, args...)
	if _, err := tc.RunCVD(cmd...); err != nil {
		log.Printf("Failed to start instance %s: %v", instanceName, err)
		return err
	}
	return nil
}

// CVDInstanceRestart performs `cvd --instance_name=<instanceName> restart <args>`.
func (tc *TestContext) CVDInstanceRestart(instanceName string, args ...string) error {
	cmd := []string{fmt.Sprintf("--instance_name=%s", instanceName), "restart"}
	cmd = append(cmd, args...)
	if _, err := tc.RunCVD(cmd...); err != nil {
		log.Printf("Failed to restart instance %s: %v", instanceName, err)
		return err
	}
	return nil
}

// CVDInstanceStatus performs `cvd --instance_name=<instanceName> status --print <args>` and parses the status JSON.
func (tc *TestContext) CVDInstanceStatus(instanceName string, args ...string) ([]CVDInstanceStatusEntry, error) {
	cmd := []string{fmt.Sprintf("--instance_name=%s", instanceName), "status", "--print"}
	cmd = append(cmd, args...)
	out, err := tc.RunCVD(cmd...)
	if err != nil {
		return nil, fmt.Errorf("cvd status failed for instance %s: %w", instanceName, err)
	}
	return ParseCVDStatusJSON(out.Stdout)
}

// CVDStop performs `cvd stop <args>` on the given TestContext.
func CVDStop(tc *TestContext, args ...string) error {
	return tc.CVDStop(args...)
}

// CVDStart performs `cvd start <args>` on the given TestContext.
func CVDStart(tc *TestContext, args ...string) error {
	return tc.CVDStart(args...)
}

// CVDRestart performs `cvd restart <args>` on the given TestContext.
func CVDRestart(tc *TestContext, args ...string) error {
	return tc.CVDRestart(args...)
}

// CVDStatus performs `cvd status --print <args>` and parses the status JSON on the given TestContext.
func CVDStatus(tc *TestContext, args ...string) ([]CVDInstanceStatusEntry, error) {
	return tc.CVDStatus(args...)
}

// CVDInstanceStop performs `cvd --instance_name=<instanceName> stop <args>` on the given TestContext.
func CVDInstanceStop(tc *TestContext, instanceName string, args ...string) error {
	return tc.CVDInstanceStop(instanceName, args...)
}

// CVDInstanceStart performs `cvd --instance_name=<instanceName> start <args>` on the given TestContext.
func CVDInstanceStart(tc *TestContext, instanceName string, args ...string) error {
	return tc.CVDInstanceStart(instanceName, args...)
}

// CVDInstanceRestart performs `cvd --instance_name=<instanceName> restart <args>` on the given TestContext.
func CVDInstanceRestart(tc *TestContext, instanceName string, args ...string) error {
	return tc.CVDInstanceRestart(instanceName, args...)
}

// CVDInstanceStatus performs `cvd --instance_name=<instanceName> status --print <args>` and parses the status JSON on the given TestContext.
func CVDInstanceStatus(tc *TestContext, instanceName string, args ...string) ([]CVDInstanceStatusEntry, error) {
	return tc.CVDInstanceStatus(instanceName, args...)
}
