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
	instance1Serial          = "127.0.0.1:6520"
	instance2Serial          = "127.0.0.1:6521"
	instance1Name            = "ins-1"
	instance2Name            = "ins-2"
	expectedInstance1Sysprop = "instance1"
	expectedInstance2Sysprop = "instance2"

	sdv2VMLoadConfig = `
{
  "instances": [
    {
      "name": "ins-1",
      "vm": {
        "cpus": 2,
        "memory_mb": 2048
      },
      "boot": {
        "extra_bootconfig_args": "androidboot.sdv.instance_name=instance1 androidboot.virt.address=3 androidboot.sdv.boot_mode=unlocked"
      },
      "security": {
        "guest_enforce_security": false
      },
      "disk": {
        "default_build": "@ab/git_main-swcar-dev/aosp_cf_x86_64_sdv_core-trunk_staging-userdebug"
      },
      "graphics": {
        "gpu_mode": "none"
      }
    },
    {
      "name": "ins-2",
      "vm": {
        "cpus": 4,
        "memory_mb": 4096
      },
      "boot": {
        "extra_bootconfig_args": "androidboot.sdv.instance_name=instance2 androidboot.virt.address=4 androidboot.sdv.boot_mode=unlocked"
      },
      "security": {
        "guest_enforce_security": false
      },
      "disk": {
        "default_build": "@ab/git_main-swcar-dev/aosp_cf_x86_64_sdv_media-trunk_staging-userdebug"
      },
      "graphics": {
        "displays": [
          {
            "width": 1920,
            "height": 1080
          }
        ],
        "gpu_mode": "gfxstream_guest_angle_host_swiftshader"
      }
    }
  ],
  "netsim_bt": false,
  "metrics": {
    "enable": true
  },
  "common": {
    "host_package": "@ab/git_main-swcar-dev/aosp_cf_x86_64_sdv_media-trunk_staging-userdebug"
  }
}`
)

func runSdv2VMTest(t *testing.T) {
	c := e2etests.TestContext{}
	c.SetUp(t)
	defer c.TearDown()

	log.Printf("Creating 2-VM SDV environment via cvd create --config_file...")
	if err := c.CVDCreateWithConfigFile(e2etests.LoadArgs{
		LoadConfig: sdv2VMLoadConfig,
	}); err != nil {
		t.Fatalf("CVDCreateWithConfigFile failed: %v", err)
	}

	log.Printf("Waiting for instance 1 (%s) to connect via adb...", instance1Serial)
	if err := c.RunAdbWaitForDeviceSerial(instance1Serial); err != nil {
		t.Fatalf("Instance 1 (%s) failed to connect to adb: %v", instance1Serial, err)
	}

	log.Printf("Waiting for instance 2 (%s) to connect via adb...", instance2Serial)
	if err := c.RunAdbWaitForDeviceSerial(instance2Serial); err != nil {
		t.Fatalf("Instance 2 (%s) failed to connect to adb: %v", instance2Serial, err)
	}

	prop1, err := c.GetSyspropStringForDevice(instance1Serial, "ro.boot.sdv.instance_name")
	if err != nil {
		t.Fatalf("failed to read ro.boot.sdv.instance_name on instance 1 (%s): %v", instance1Serial, err)
	}
	if prop1 != expectedInstance1Sysprop {
		t.Fatalf("unexpected instance name on instance 1: expected %q, got %q", expectedInstance1Sysprop, prop1)
	}

	prop2, err := c.GetSyspropStringForDevice(instance2Serial, "ro.boot.sdv.instance_name")
	if err != nil {
		t.Fatalf("failed to read ro.boot.sdv.instance_name on instance 2 (%s): %v", instance2Serial, err)
	}
	if prop2 != expectedInstance2Sysprop {
		t.Fatalf("unexpected instance name on instance 2: expected %q, got %q", expectedInstance2Sysprop, prop2)
	}
	log.Printf("Verified initial 2-VM state: %s=%s, %s=%s", instance1Serial, prop1, instance2Serial, prop2)

	verifyBothReachable := func(stage string) {
		if !c.IsAdbShellReachable(instance1Serial) {
			t.Fatalf("Device 1 (%s) is not reachable after %s", instance1Serial, stage)
		}
		if !c.IsAdbShellReachable(instance2Serial) {
			t.Fatalf("Device 2 (%s) is not reachable after %s", instance2Serial, stage)
		}
	}

	t.Run("Status", func(t *testing.T) {
		for _, inst := range []struct {
			name   string
			serial string
		}{
			{instance1Name, instance1Serial},
			{instance2Name, instance2Serial},
		} {
			out, err := c.CVDInstanceStatus(inst.name, "--print")
			if err != nil {
				t.Fatalf("CVDInstanceStatus failed for %s: %v", inst.name, err)
			}
			entries, err := e2etests.ParseCVDStatusJSON(out.Stdout)
			if err != nil {
				t.Fatalf("Failed to parse status output for %s: %v, raw output:\n%s", inst.name, err, out.Stdout)
			}
			if len(entries) == 0 {
				t.Fatalf("Expected at least 1 status entry for %s, got 0", inst.name)
			}
			found := false
			for _, entry := range entries {
				if entry.InstanceName == inst.name || entry.InstanceName == "" || len(entries) == 1 {
					if !strings.EqualFold(entry.Status, "Running") {
						t.Fatalf("Expected status 'Running' for %s, got %q", inst.name, entry.Status)
					}
					found = true
					log.Printf("Status check passed for %s: %s", inst.name, entry.Status)
					break
				}
			}
			if !found {
				t.Fatalf("No status entry matching %s found in: %v", inst.name, entries)
			}
			if !c.IsAdbShellReachable(inst.serial) {
				t.Fatalf("ADB shell is not reachable for %s (%s)", inst.name, inst.serial)
			}
		}
		verifyBothReachable("Status subtest")
	})

	// Note: In Cuttlefish multi-instance setups, Instance 1 (ins-1) hosts the common
	// environment daemons (e.g. shared networking and sockets). As a result,
	// Instance 1 cannot be stopped or restarted independently without disrupting the
	// entire environment. Therefore, lifecycle operations (stop/start and restart)
	// are performed on Instance 2 (ins-2), verifying that Instance 1 remains online
	// and unaffected throughout.
	t.Run("StopStartInstance2_IsolateInstance1", func(t *testing.T) {
		log.Printf("Stopping %s...", instance2Name)
		if err := c.CVDInstanceStop(instance2Name); err != nil {
			t.Fatalf("CVDInstanceStop(%s) failed: %v", instance2Name, err)
		}

		log.Printf("Verifying %s (%s) went offline...", instance2Name, instance2Serial)
		if err := c.WaitForDeviceOffline(instance2Serial, 30); err != nil {
			t.Fatalf("Instance 2 (%s) did not go offline after stop: %v", instance2Serial, err)
		}

		// Verify non-targeted VM (ins-1) remains online and fully reachable
		log.Printf("Verifying non-targeted instance %s (%s) remains online...", instance1Name, instance1Serial)
		if !c.IsAdbShellReachable(instance1Serial) {
			t.Fatalf("Isolation violation: Instance 1 (%s) became unreachable when Instance 2 was stopped", instance1Serial)
		}

		log.Printf("Starting %s...", instance2Name)
		if err := c.CVDInstanceStart(instance2Name); err != nil {
			t.Fatalf("CVDInstanceStart(%s) failed: %v", instance2Name, err)
		}

		log.Printf("Waiting for %s (%s) to come back online...", instance2Name, instance2Serial)
		if err := c.WaitForDeviceOnline(instance2Serial, 45); err != nil {
			t.Fatalf("Instance 2 (%s) did not come back online after start: %v", instance2Serial, err)
		}

		nameAfter, err := c.GetSyspropStringForDevice(instance2Serial, "ro.boot.sdv.instance_name")
		if err != nil {
			t.Fatalf("failed to read ro.boot.sdv.instance_name on %s: %v", instance2Serial, err)
		}
		if nameAfter != expectedInstance2Sysprop {
			t.Fatalf("instance name changed on %s after start: expected %q, got %q", instance2Serial, expectedInstance2Sysprop, nameAfter)
		}
		log.Printf("Stop/Start check passed for %s with full isolation on %s", instance2Name, instance1Name)
		verifyBothReachable("StopStartInstance2 subtest")
	})

	t.Run("RestartInstance2", func(t *testing.T) {
		log.Printf("Restarting %s...", instance2Name)
		if err := c.CVDInstanceRestart(instance2Name); err != nil {
			t.Fatalf("CVDInstanceRestart(%s) failed: %v", instance2Name, err)
		}

		// Verify non-targeted VM (ins-1) remains online and fully reachable
		log.Printf("Verifying non-targeted instance %s (%s) remains online...", instance1Name, instance1Serial)
		if !c.IsAdbShellReachable(instance1Serial) {
			t.Fatalf("Isolation violation: Instance 1 (%s) became unreachable when Instance 2 was restarted", instance1Serial)
		}

		log.Printf("Waiting for %s (%s) to come back online...", instance2Name, instance2Serial)
		if err := c.WaitForDeviceOnline(instance2Serial, 45); err != nil {
			t.Fatalf("Instance 2 (%s) did not come back online after restart: %v", instance2Serial, err)
		}

		nameAfter, err := c.GetSyspropStringForDevice(instance2Serial, "ro.boot.sdv.instance_name")
		if err != nil {
			t.Fatalf("failed to read ro.boot.sdv.instance_name on %s: %v", instance2Serial, err)
		}
		if nameAfter != expectedInstance2Sysprop {
			t.Fatalf("instance name changed on %s after restart: expected %q, got %q", instance2Serial, expectedInstance2Sysprop, nameAfter)
		}
		log.Printf("Restart check passed for %s with full isolation on %s", instance2Name, instance1Name)
		verifyBothReachable("RestartInstance2 subtest")
	})
}
