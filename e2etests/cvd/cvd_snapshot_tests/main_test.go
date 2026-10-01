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
	"path/filepath"
	"testing"

	"github.com/google/android-cuttlefish/e2etests/cvd/common"
)

func TestCvdSnapshot(t *testing.T) {
	testcases := []struct {
		branch string
		target string
	}{
		{
			branch: "git_main",
			target: "aosp_cf_x86_64_only_phone-trunk_staging-userdebug",
		},
	}
	const tmpFile = "/data/local/tmp/foo"
	for _, tc := range testcases {
		t.Run(fmt.Sprintf("BUILD=%s/%s", tc.branch, tc.target), func(t *testing.T) {
			c := e2etests.TestContext{}
			c.SetUp(t)
			defer c.TearDown()

			if _, err := c.CVDFetch(e2etests.FetchArgs{
				DefaultBuildBranch: tc.branch,
				DefaultBuildTarget: tc.target,
			}); err != nil {
				t.Fatal(err)
			}

			createOut, err := c.CVDCreate(e2etests.CreateArgs{
				Args: []string{
					"--gpu_mode=guest_swiftshader",
					"--enable_virtiofs=false",
					"--enable_usb=false",
					"--snapshot_compatible=true",
					"--vhost_user_vsock=true",
				},
			})
			if err != nil {
				t.Fatal(err)
			}

			entries, err := e2etests.ParseCVDStatusJSON(createOut.Stdout)
			if err != nil || len(entries) == 0 || entries[0].AdbSerial == "" {
				t.Fatalf("failed to parse adb_serial from cvd create output: %v", err)
			}
			serial := entries[0].AdbSerial

			if err := c.RunAdbWaitForDeviceSerial(serial); err != nil {
				t.Fatalf("failed to wait for Cuttlefish device %s to connect to adb: %v", serial, err)
			}

			if _, err := c.RunCmd("adb", "-s", serial, "shell", "touch", tmpFile); err != nil {
				t.Fatalf("failed to create %s: %v", tmpFile, err)
			}

			if _, err := c.RunCmd("adb", "-s", serial, "shell", "stat", tmpFile); err != nil {
				t.Fatalf("failed to verify %s created: %v", tmpFile, err)
			}

			if _, err := c.RunCVD("suspend"); err != nil {
				t.Fatalf("failed to suspend instance: %v", err)
			}

			snapshotPath := filepath.Join(t.TempDir(), "snapshot")
			if _, err := c.RunCVD("snapshot_take", "--snapshot_path="+snapshotPath); err != nil {
				t.Fatalf("failed to take snapshot: %v", err)
			}

			if _, err := c.RunCVD("resume"); err != nil {
				t.Fatalf("failed to resume instance: %v", err)
			}

			if _, err := c.RunCmd("adb", "-s", serial, "shell", "rm", tmpFile); err != nil {
				t.Fatalf("failed to remove %s after snapshot: %v", tmpFile, err)
			}

			if err := c.CVDStop(); err != nil {
				t.Fatalf("failed to stop instance: %v", err)
			}

			if err := c.WaitForDeviceOffline(serial, 30); err != nil {
				t.Fatalf("device %s did not go offline after stop: %v", serial, err)
			}

			if err := c.CVDStart("--snapshot_path=" + snapshotPath); err != nil {
				t.Fatalf("failed to start instance from snapshot: %v", err)
			}

			if err := c.RunAdbWaitForDeviceSerial(serial); err != nil {
				t.Fatalf("failed to wait for Cuttlefish device %s to connect to adb after restore: %v", serial, err)
			}

			if _, err := c.RunCmd("adb", "-s", serial, "shell", "stat", tmpFile); err != nil {
				t.Fatalf("failed to stat %s after snapshot restore: %v", tmpFile, err)
			}
		})
	}
}
