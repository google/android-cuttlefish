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
	"slices"
	"strings"
	"testing"

	"github.com/google/android-cuttlefish/e2etests/cvd/common"
)

func TestPrintLogs(t *testing.T) {
	c := e2etests.TestContext{}
	c.SetUp(t)
	defer c.TearDown()

	env_config := `
{
  "instances": [
    {
      "name": "ins-1",
      "disk": {
        "default_build": "@ab\/git_main\/aosp_cf_x86_64_only_phone-trunk_staging-userdebug"
      },
      "vm": {
        "cpus": 4,
        "memory_mb": 4096,
        "setupwizard_mode": "REQUIRED"
      },
      "graphics": {
        "displays": [
          {
            "width": 720,
            "height": 1280,
            "dpi": 140,
            "refresh_rate_hertz": 60
          }
        ],
        "record_screen": false
      }
    }
  ],
  "netsim_bt": false,
  "metrics": {
    "enable": true
  },
  "common": {
    "host_package": "@ab\/git_main\/aosp_cf_x86_64_only_phone-trunk_staging-userdebug"
  }
}
`

	if err := c.CVDLoad(e2etests.LoadArgs{LoadConfig: env_config}); err != nil {
		t.Fatal(err)
	}

	res, err := c.RunCmd(c.TargetBin(), "logs")
	if err != nil {
		t.Fatal(err)
	}
	stdOut := res.Stdout

	listedNames := []string{}
	lines := strings.Split(stdOut, "\n")
	for _, line := range lines[:len(lines)-1] {
		fields := strings.Fields(line)
		if len(fields) != 2 {
			t.Errorf("line %q does not match format '<NAME> <PATH>'", line)
		}
		res, err := c.RunCmd("stat", fields[1])
		if err != nil {
			t.Fatal(res.Stderr)
		}
		listedNames = append(listedNames, fields[0])
	}
	keyExpectedNames := []string{
		// Group-level logs
		"assemble_cvd.log",
		"fetch.log",
		// Instance-level logs
		"kernel.log",
		"launcher.log",
		"logcat",
	}
	for _, name := range keyExpectedNames {
		if !slices.Contains(listedNames, name) {
			t.Fatalf("missing log name: %q", name)
		}
	}

	res, err = c.RunCmd(c.TargetBin(), "logs", "--print", "launcher.log")
	if err != nil {
		t.Fatal(err)
	}
	stdOut = res.Stdout
	if len(stdOut) == 0 {
		t.Fatalf("empty launcher.log")
	}

}
