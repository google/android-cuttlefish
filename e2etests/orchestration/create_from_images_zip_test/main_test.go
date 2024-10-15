// Copyright (C) 2024 The Android Open Source Project
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
	"testing"

	"github.com/google/android-cuttlefish/e2etests/orchestration/common"

	hoapi "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	hoclient "github.com/google/android-cuttlefish/frontend/src/libhoclient"
	"github.com/google/go-cmp/cmp"
)

func TestInstance(t *testing.T) {
	ctx, err := common.Setup(61002)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() {
		common.Cleanup(ctx)
	})
	srv := hoclient.NewHostOrchestratorService(ctx.ServiceURL)
	uploadDir, err := srv.CreateUploadDir()
	if err != nil {
		t.Fatal(err)
	}
	if err := common.UploadAndExtract(srv, uploadDir, "../artifacts/images.zip"); err != nil {
		t.Fatal(err)
	}
	if err := common.UploadAndExtract(srv, uploadDir, "../artifacts/cvd-host_package.tar.gz"); err != nil {
		t.Fatal(err)
	}
	const group_name = "foo"
	config := `
  {
    "common": {
      "group_name": "` + group_name + `",
      "host_package": "@user_artifacts/` + uploadDir + `"

    },
    "instances": [
      {
        "vm": {
          "memory_mb": 8192,
          "setupwizard_mode": "OPTIONAL",
          "cpus": 8
        },
        "disk": {
          "default_build": "@user_artifacts/` + uploadDir + `"
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

	got, createErr := srv.CreateCVD(createReq, hoclient.BuildAPICredential{})

	if err := common.DownloadHostBugReport(srv, group_name); err != nil {
		t.Errorf("failed creating bugreport: %s\n", err)
	}
	if createErr != nil {
		t.Fatal(createErr)
	}
	want := &hoapi.CreateCVDResponse{
		CVDs: []*hoapi.CVD{
			&hoapi.CVD{
				Group:          "foo",
				Name:           "1",
				BuildSource:    &hoapi.BuildSource{},
				Status:         "Running",
				Displays:       []string{"720 x 1280 ( 320 )"},
				WebRTCDeviceID: "cvd-1",
				ADBSerial:      "0.0.0.0:6520",
			},
		},
	}
	if diff := cmp.Diff(want, got); diff != "" {
		t.Errorf("response mismatch (-want +got):\n%s", diff)
	}
}
