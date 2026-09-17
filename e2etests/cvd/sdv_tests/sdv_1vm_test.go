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
	"log"
	"strings"
	"testing"

	"github.com/google/android-cuttlefish/e2etests/cvd/common"
)

const (
	sdvBranch          = "git_main-swcar-dev"
	sdvCoreTarget      = "aosp_cf_x86_64_sdv_core-trunk_staging-userdebug"
	defaultSerial      = "127.0.0.1:6520"
	expected1VMSysprop = "instance1"
	expectedBootMode   = "locked"

	// Bootconfig args matching sdv_default_extra_launch_args from CI configuration
	// (configs/wireless/android/testing/atp/prod/aaos-bi-engprod/common/cvd_vms_setup/cvd_default_args.gcl).
	extraBootconfigArgs = "--extra_bootconfig_args=" +
		"androidboot.sdv.boot_mode=locked " +
		"androidboot.sdv.ignore_avb_state=true " +
		"androidboot.sdv.init_open_dice.sample_file=dice_handover_instance1 " +
		"androidboot.sdv.instance_name=instance1 " +
		"androidboot.sdv.keymint.rpc.hbk=799da7577efd41d5b27810c5952fcec0291cbcfd687e77ac9a6cec8370651b1d " +
		"androidboot.sdv.preprovisioned_vvmtruststore=etc " +
		"androidboot.sdv.someip.enable=true " +
		"androidboot.sdv.vvmfactorytrust=00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff " +
		"androidboot.virt.address=3"
)

func runSdv1VMTest(t *testing.T) {
	c := e2etests.TestContext{}
	c.SetUp(t)
	defer c.TearDown()

	log.Printf("Fetching SDV Core image (%s/%s)...", sdvBranch, sdvCoreTarget)
	if _, err := c.CVDFetch(e2etests.FetchArgs{
		DefaultBuildBranch: sdvBranch,
		DefaultBuildTarget: sdvCoreTarget,
	}); err != nil {
		t.Fatalf("CVDFetch failed: %v", err)
	}

	log.Printf("Creating 1-VM SDV Core instance...")
	if _, err := c.CVDCreate(e2etests.CreateArgs{
		Args: []string{extraBootconfigArgs},
	}); err != nil {
		t.Fatalf("CVDCreate failed: %v", err)
	}

	log.Printf("Waiting for device %s to be available via adb...", defaultSerial)
	if err := c.RunAdbWaitForDeviceSerial(defaultSerial); err != nil {
		t.Fatalf("timed out waiting for device %s: %v", defaultSerial, err)
	}

	initialName, err := c.GetSyspropStringForDevice(defaultSerial, "ro.boot.sdv.instance_name")
	if err != nil {
		t.Fatalf("failed to read initial ro.boot.sdv.instance_name: %v", err)
	}
	if initialName != expected1VMSysprop {
		t.Fatalf("unexpected instance name: expected %q, got %q", expected1VMSysprop, initialName)
	}
	log.Printf("Verified initial instance name: %s", initialName)

	initialBootMode, err := c.GetSyspropStringForDevice(defaultSerial, "ro.boot.sdv.boot_mode")
	if err != nil {
		t.Fatalf("failed to read initial ro.boot.sdv.boot_mode: %v", err)
	}
	if initialBootMode != expectedBootMode {
		t.Fatalf("unexpected boot mode: expected %q, got %q", expectedBootMode, initialBootMode)
	}
	log.Printf("Verified initial boot mode: %s", initialBootMode)

	t.Run("Status", func(t *testing.T) {
		entries, err := c.CVDStatus()
		if err != nil {
			t.Fatalf("CVDStatus failed: %v", err)
		}
		if len(entries) == 0 {
			t.Fatalf("Expected at least 1 instance status entry, got 0")
		}
		if !strings.EqualFold(entries[0].Status, "Running") {
			t.Fatalf("Expected instance status 'Running', got %q", entries[0].Status)
		}
		if !c.IsAdbShellReachable(defaultSerial) {
			t.Fatalf("ADB shell is not reachable for device %s", defaultSerial)
		}
		log.Printf("Status check passed for 1-VM: %s", entries[0].Status)
	})

	t.Run("StopStart", func(t *testing.T) {
		log.Printf("Stopping 1-VM instance...")
		if err := c.CVDStop(); err != nil {
			t.Fatalf("CVDStop failed: %v", err)
		}

		log.Printf("Verifying device %s went offline...", defaultSerial)
		if err := c.WaitForDeviceOffline(defaultSerial, 30); err != nil {
			t.Fatalf("device did not go offline after CVDStop: %v", err)
		}

		log.Printf("Starting 1-VM instance...")
		if err := c.CVDStart(); err != nil {
			t.Fatalf("CVDStart failed: %v", err)
		}

		log.Printf("Waiting for device %s to reconnect...", defaultSerial)
		if err := c.RunAdbWaitForDeviceSerial(defaultSerial); err != nil {
			t.Fatalf("device did not come back online after CVDStart: %v", err)
		}

		nameAfter, err := c.GetSyspropStringForDevice(defaultSerial, "ro.boot.sdv.instance_name")
		if err != nil {
			t.Fatalf("failed to read ro.boot.sdv.instance_name after start: %v", err)
		}
		if nameAfter != expected1VMSysprop {
			t.Fatalf("instance name changed after start: expected %q, got %q", expected1VMSysprop, nameAfter)
		}
		log.Printf("Stop/Start check passed for 1-VM; instance name preserved: %s", nameAfter)
	})

	t.Run("Restart", func(t *testing.T) {
		log.Printf("Restarting 1-VM instance...")
		if err := c.CVDRestart(); err != nil {
			t.Fatalf("CVDRestart failed: %v", err)
		}

		log.Printf("Waiting for device %s to be online and responsive after restart...", defaultSerial)
		if err := c.RunAdbWaitForDeviceSerial(defaultSerial); err != nil {
			t.Fatalf("device did not come back online after CVDRestart: %v", err)
		}

		nameAfter, err := c.GetSyspropStringForDevice(defaultSerial, "ro.boot.sdv.instance_name")
		if err != nil {
			t.Fatalf("failed to read ro.boot.sdv.instance_name after restart: %v", err)
		}
		if nameAfter != expected1VMSysprop {
			t.Fatalf("instance name changed after restart: expected %q, got %q", expected1VMSysprop, nameAfter)
		}
		log.Printf("Restart check passed for 1-VM; instance name preserved: %s", nameAfter)
	})
}
