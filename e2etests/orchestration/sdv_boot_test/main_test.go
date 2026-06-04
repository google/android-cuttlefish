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
	"time"

	"github.com/google/android-cuttlefish/e2etests/orchestration/common"

	hoapi "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	hoclient "github.com/google/android-cuttlefish/frontend/src/libhoclient"
)

const baseURL = "http://0.0.0.0:2080"

func TestSDVBoot(t *testing.T) {
	srv := hoclient.NewHostOrchestratorClient(baseURL)
	t.Cleanup(func() {
		if err := common.CollectHOLogs(baseURL); err != nil {
			log.Printf("failed to collect HO logs: %s", err)
		}
	})

	hostPkgDir, err := common.PrepareArtifact(srv, "../artifacts/cvd-host_package.tar.gz")
	if err != nil {
		t.Fatal(err)
	}
	sdvCoreDir, err := common.PrepareArtifact(srv, "../artifacts/sdv_core_images.zip")
	if err != nil {
		t.Fatal(err)
	}
	sdvMediaDir, err := common.PrepareArtifact(srv, "../artifacts/sdv_media_images.zip")
	if err != nil {
		t.Fatal(err)
	}

	config := `
  {
    "common": {
      "group_name": "sdv-system",
      "host_package": "@image_dirs/` + hostPkgDir + `"
    },
    "instances": [
      {
        "name": "core",
        "vm": { "memory_mb": 4096, "cpus": 4 },
        "disk": { "default_build": "@image_dirs/` + sdvCoreDir + `" }
      },
      {
        "name": "media",
        "vm": { "memory_mb": 4096, "cpus": 4 },
        "disk": { "default_build": "@image_dirs/` + sdvMediaDir + `" }
      }
    ]
  }
  `
	envConfig := make(map[string]interface{})
	if err := json.Unmarshal([]byte(config), &envConfig); err != nil {
		t.Fatal(err)
	}
	createReq := &hoapi.CreateCVDRequest{EnvConfig: envConfig}

	res, err := srv.CreateCVD(createReq, &hoclient.AccessTokenBuildAPICreds{})
	if err != nil {
		t.Fatalf("failed creating SDV environment: %v", err)
	}

	adb := common.NewAdbHelper()
	for _, cvd := range res.CVDs {
		t.Run("VerifyBoot_"+cvd.Name, func(t *testing.T) {
			serial := cvd.ADBSerial
			if err := adb.WaitForDevice(serial); err != nil {
				t.Fatalf("Device %s failed to appear: %v", serial, err)
			}

			// Wait for boot completed property
			deadline := time.Now().Add(5 * time.Minute)
			booted := false
			for time.Now().Before(deadline) {
				out, err := adb.ExecShellCommand(serial, []string{"getprop", "sys.boot_completed"})
				if err == nil && out == "1\n" {
					booted = true
					break
				}
				time.Sleep(10 * time.Second)
			}

			if !booted {
				t.Errorf("Device %s failed to boot within timeout", serial)
			}
		})
	}
}
