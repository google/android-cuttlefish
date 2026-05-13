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
	"testing"

	"github.com/google/android-cuttlefish/e2etests/cvd/common"
)

func TestEmulatedCameraV4l2Compliance(t *testing.T) {
	testcases := []struct {
		branch     string
		target     string
		cameraType string
	}{
		{
			branch:     "15346462", // TODO(b/510415749)
			target:     "aosp_cf_x86_64_only_phone-trunk_staging-userdebug",
			cameraType: "v4l2_emulated_camera_splane",
		},
		{
			branch:     "15346462", // TODO(b/510415749)
			target:     "aosp_cf_x86_64_only_phone-trunk_staging-userdebug",
			cameraType: "v4l2_emulated_camera_mplane",
		},
	}
	c := e2etests.TestContext{}
	for _, tc := range testcases {
		t.Run(fmt.Sprintf("BUILD=%s/%s/TYPE=%s", tc.branch, tc.target, tc.cameraType), func(t *testing.T) {
			c.SetUp(t)
			defer c.TearDown()

			if err := c.CVDFetch(e2etests.FetchArgs{
				DefaultBuildBranch: tc.branch,
				DefaultBuildTarget: tc.target,
			}); err != nil {
				t.Fatal(err)
			}

			if err := c.CVDCreate(e2etests.CreateArgs{Args: []string{fmt.Sprintf("--media=type=%s", tc.cameraType)}}); err != nil {
				t.Fatal(err)
			}

			if err := c.RunAdbWaitForDevice(); err != nil {
				t.Fatalf("failed to wait for Cuttlefish device to connect to adb: %w", err)
			}

			if _, err := c.RunCmd("adb", "shell", "su", "0", "v4l2-ctl", "--list-devices"); err != nil {
				t.Fatalf("v4l2-ctl --list-devices failed: %w", err)
			}

			if _, err := c.RunCmd("adb", "shell", "su", "0", "v4l2-compliance", "-d1", "-s"); err != nil {
				t.Fatalf("v4l2-compliance failed: %w", err)
			}
		})
	}
}
