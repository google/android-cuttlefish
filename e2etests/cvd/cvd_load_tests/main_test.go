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

func TestCvdLoad(t *testing.T) {
	testcases := []struct {
		name string
		loadconfig string
	}{
		{
			name: "GitMainX64Phone",
			loadconfig: `
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
}`,
		},
		{
			name: "AospMainX64Phone",
			loadconfig: `
{
  "instances": [
    {
      "name": "ins-1",
      "disk": {
        "default_build": "@ab\/aosp-android-latest-release\/aosp_cf_x86_64_only_phone-userdebug",
        "super": {
          "system": "@ab\/aosp-android-latest-release\/aosp_cf_x86_64_only_phone-userdebug"
        }
      },
      "boot": {
        "kernel": {
          "build": "@ab\/aosp_kernel-common-android16-6.12\/kernel_virt_x86_64"
        },
        "build": "@ab\/aosp-android-latest-release\/aosp_cf_x86_64_only_phone-userdebug"
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
    "host_package": "@ab\/aosp-android-latest-release\/aosp_cf_x86_64_only_phone-userdebug"
  }
}`,
		},
    {
      name: "AospMainX64PhoneX2",
      loadconfig: `
{
  "instances": [
    {
      "name": "ins-1",
      "disk": {
        "default_build": "@ab\/aosp-android-latest-release\/aosp_cf_x86_64_only_phone-userdebug"
      }
    },
    {
      "name": "ins-2",
      "disk": {
        "default_build": "@ab\/aosp-android-latest-release\/aosp_cf_x86_64_only_phone-userdebug"
      }
    }
  ],
  "metrics": {
    "enable": true
  },
  "common": {
    "host_package": "@ab\/aosp-android-latest-release\/aosp_cf_x86_64_only_phone-userdebug"
  }
}`,
    },
	}
	for _, tc := range testcases {
		t.Run(fmt.Sprintf("BUILD=%s", tc.name), func(t *testing.T) {
			c := e2etests.TestContext{}
      c.SetUp(t)

			if err := c.CVDLoad(e2etests.LoadArgs{
        LoadConfig: tc.loadconfig,
      }); err != nil {
        t.Fatal(err)
      }

      c.TearDown()
		})
	}
}
