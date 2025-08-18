// Copyright (C) 2025 The Android Open Source Project
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
)

const baseURL = "http://0.0.0.0:2080"

func TestReset(t *testing.T) {
	srv := hoclient.NewHostOrchestratorClient(baseURL)
	t.Cleanup(func() {
		if err := common.CollectHOLogs(baseURL); err != nil {
			log.Printf("failed to collect HO logs: %s", err)
		}
	})

	// Create a cvd
	imageDir, err := common.PrepareArtifact(srv, "../artifacts/images.zip")
	if err != nil {
		t.Fatal(err)
	}
	hostPkgDir, err := common.PrepareArtifact(srv, "../artifacts/cvd-host_package.tar.gz")
	if err != nil {
		t.Fatal(err)
	}
	const group_name = "foo"
	config := `
  {
    "common": {
      "group_name": "` + group_name + `",
      "host_package": "@image_dirs/` + hostPkgDir + `"

    },
    "instances": [
      {
        "vm": {
          "memory_mb": 8192,
          "setupwizard_mode": "OPTIONAL",
          "cpus": 8
        },
        "disk": {
          "default_build": "@image_dirs/` + imageDir + `"
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

	// Make sure there are cvds in the host
	cvds, err := srv.ListCVDs()
	if err != nil {
		t.Fatal(err)
	}
	if len(cvds) == 0 {
		t.Fatal("ListCVDs returned empty list after create-from-images-dirs")
	}

	// Reset the host
	if err := srv.Reset(); err != nil {
		t.Fatal(err)
	}

	// Check that there are no cvds left in the host
	cvds, err = srv.ListCVDs()
	if err != nil {
		t.Fatal(err)
	}
	if len(cvds) != 0 {
		t.Fatal("ListCVDs returned non-empty list after reset")
	}
}
