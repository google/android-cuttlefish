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
	"encoding/json"
	"log"
	"testing"

	"github.com/google/android-cuttlefish/e2etests/orchestration/common"

	hoapi "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	hoclient "github.com/google/android-cuttlefish/frontend/src/libhoclient"
	"github.com/google/go-cmp/cmp"
)

const baseURL = "http://0.0.0.0:2080"

func TestStopStart(t *testing.T) {
	srv := hoclient.NewHostOrchestratorClient(baseURL)
	t.Cleanup(func() {
		if err := common.CollectHOLogs(baseURL); err != nil {
			log.Printf("failed to collect HO logs: %s", err)
		}
	})
	config := `
  {
    "instances": [
      {
        "vm": {
					"crosvm": {
						"enable_sandbox": false
					},
          "memory_mb": 8192,
          "setupwizard_mode": "OPTIONAL",
          "cpus": 8
        },
        "disk": {
	 				"default_build": "@ab/aosp-android-latest-release/aosp_cf_x86_64_only_phone-userdebug",
					"download_img_zip": true
        },
        "streaming": {
          "device_id": "cvd-1"
        }
      },
      {
        "vm": {
					"crosvm": {
						"enable_sandbox": false
					},
          "memory_mb": 8192,
          "setupwizard_mode": "OPTIONAL",
          "cpus": 8
        },
        "disk": {
	 				"default_build": "@ab/aosp-android-latest-release/aosp_cf_x86_64_only_phone-userdebug",
					"download_img_zip": true
        },
        "streaming": {
          "device_id": "cvd-1"
        }
      }
    ]
  }
  `
	envConfig := make(map[string]interface{})
	if err := json.Unmarshal([]byte(config), &envConfig); err != nil {
		t.Fatal(err)
	}
	createReq := &hoapi.CreateCVDRequest{
		EnvConfig: envConfig,
	}
	if _, err := srv.CreateCVD(createReq, &hoclient.AccessTokenBuildAPICreds{}); err != nil {
		t.Fatal(err)
	}
	cvds, err := srv.ListCVDs()
	if err != nil {
		t.Fatal(err)
	}
	if diff := cmp.Diff(cvdStatus(cvds), []string{"Running", "Running"}); diff != "" {
		t.Fatalf("status mismatch (-want +got):\n%s", diff)
	}

	if err := srv.StopGroup("cvd_1"); err != nil {
		t.Fatal(err)
	}
	cvds, err = srv.ListCVDs()
	if err != nil {
		t.Fatal(err)
	}
	if diff := cmp.Diff(cvdStatus(cvds), []string{"Stopped", "Stopped"}); diff != "" {
		t.Fatalf("status mismatch (-want +got):\n%s", diff)
	}

	if err := srv.StartGroup("cvd_1", &hoapi.StartCVDRequest{}); err != nil {
		t.Fatal(err)
	}
	cvds, err = srv.ListCVDs()
	if err != nil {
		t.Fatal(err)
	}
	if diff := cmp.Diff(cvdStatus(cvds), []string{"Running", "Running"}); diff != "" {
		t.Fatalf("status mismatch (-want +got):\n%s", diff)
	}
}

func cvdStatus(cvds []*hoapi.CVD) []string {
    res := make([]string, len(cvds))
	  for i, v := range cvds {
        res[i] = v.Status
    }
		return res
}
