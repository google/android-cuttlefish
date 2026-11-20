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

package main

import (
	"fmt"
	"strings"
	"testing"

	"github.com/google/android-cuttlefish/e2etests/cvd/common"
)

// Injected at link time via `x_defs` in def.bzl.
var (
	branch string
	target string
)

const mediaType = "v4l2_emulated_camera_mplane_external"

func TestEmulatedCameraV4l2Compliance(t *testing.T) {
	if branch == "" || target == "" {
		t.Fatalf("branch and target must be set via x_defs in def.bzl (branch=%q, target=%q)", branch, target)
	}

	c := e2etests.TestContext{}
	c.SetUp(t)
	defer c.TearDown()

	if _, err := c.CVDFetch(e2etests.FetchArgs{
		DefaultBuildBranch: branch,
		DefaultBuildTarget: target,
	}); err != nil {
		t.Fatal(err)
	}

	if _, err := c.CVDCreate(e2etests.CreateArgs{
		Args: []string{
			fmt.Sprintf("--media=%s", mediaType),
		},
	}); err != nil {
		t.Fatal(err)
	}

	if err := c.RunAdbWaitForDevice(); err != nil {
		t.Fatalf("failed to wait for Cuttlefish device to connect to adb: %v", err)
	}

	out, err := c.RunCmd("adb", "shell", "su", "0", "v4l2-ctl", "--list-devices")
	if err != nil {
		t.Fatalf("v4l2-ctl --list-devices failed: %v", err)
	}
	videoNode, err := findVideoNode(out.Stdout, "emulated_camera")
	if err != nil {
		t.Fatal(err)
	}

	if _, err := c.RunCmd("adb", "shell", "su", "0", "v4l2-compliance", "-d", videoNode, "-s"); err != nil {
		t.Fatalf("v4l2-compliance failed: %v", err)
	}
}

// findVideoNode returns the first /dev/videoN node listed under the `v4l2-ctl --list-devices`
// entry whose card name contains cardName, e.g.:
//
//	emulated_camera (platform:virtio-media):
//		/dev/video0
//
// Multiple cameras can be attached, but for now this test only exercises the first one.
func findVideoNode(listDevicesOutput, cardName string) (string, error) {
	cardMatched := false
	for _, line := range strings.Split(listDevicesOutput, "\n") {
		trimmed := strings.TrimSpace(line)
		switch {
		case trimmed == "":
			cardMatched = false
		case !strings.HasPrefix(line, "\t"):
			cardMatched = strings.Contains(trimmed, cardName)
		case cardMatched && strings.HasPrefix(trimmed, "/dev/video"):
			return trimmed, nil
		}
	}
	return "", fmt.Errorf("%q device not found in guest:\n%s", cardName, listDevicesOutput)
}
