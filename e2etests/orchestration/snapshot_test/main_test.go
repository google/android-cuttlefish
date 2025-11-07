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
	"errors"
	"fmt"
	"log"
	"os/exec"
	"strings"
	"testing"

	"github.com/google/android-cuttlefish/e2etests/orchestration/common"

	hoapi "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	hoclient "github.com/google/android-cuttlefish/frontend/src/libhoclient"
	"github.com/google/go-cmp/cmp"
)

const baseURL = "http://0.0.0.0:2080"

func TestSnapshot(t *testing.T) {
	adbH := common.NewAdbHelper()
	if err := adbH.StartServer(); err != nil {
		t.Fatal(err)
	}
	srv := hoclient.NewHostOrchestratorClient(baseURL)
	t.Cleanup(func() {
		if err := common.CollectHOLogs(baseURL); err != nil {
			log.Printf("failed to collect HO logs: %s", err)
		}
	})
	imageDir, err := common.PrepareArtifact(srv, "../artifacts/images.zip")
	if err != nil {
		t.Fatal(err)
	}
	hostPkgDir, err := common.PrepareArtifact(srv, "../artifacts/cvd-host_package.tar.gz")
	if err != nil {
		t.Fatal(err)
	}
	const groupName = "cvd"
	cvd, err := createDevice(srv, groupName, hostPkgDir, imageDir)
	if err != nil {
		t.Fatal(err)
	}
	if err := adbH.Connect(cvd.ADBSerial); err != nil {
		t.Fatal(err)
	}
	const tmpFile = "/data/local/tmp/foo"
	// Create temporary file
	if _, err := adbH.ExecShellCommand(cvd.ADBSerial, []string{"touch", tmpFile}); err != nil {
		t.Fatal(err)
	}
	snapshotID := "snapshot_00000000-0000-0000-0000-000000000000"
	// Create a snapshot containing the temporary file.
	createSnapshotRes, err := srv.CreateSnapshot(groupName, cvd.Name, &hoapi.CreateSnapshotRequest{SnapshotID: snapshotID})
	if err != nil {
		t.Fatal(err)
	}
	if diff := cmp.Diff(snapshotID, createSnapshotRes.SnapshotID); diff != "" {
		t.Fatalf("snapshot id mismatch (-want +got):\n%s", diff)
	}
	// Powerwash the device removing the temporary file.
	if err := srv.Powerwash(groupName, cvd.Name); err != nil {
		t.Fatal(err)
	}
	// Verifies temporary file does not exist.
	_, err = adbH.ExecShellCommand(cvd.ADBSerial, []string{"stat", tmpFile})
	var exitCodeErr *exec.ExitError
	if !errors.As(err, &exitCodeErr) {
		t.Fatal(err)
	}
	// Stop the device.
	if err := srv.Stop(groupName, cvd.Name); err != nil {
		t.Fatal(err)
	}
	// Restore the device from the snapshot.
	req := &hoapi.StartCVDRequest{SnapshotID: snapshotID}
	if err := srv.Start(groupName, cvd.Name, req); err != nil {
		t.Fatal(err)
	}
	cvd, err = getCVD(srv)
	if err != nil {
		t.Fatal(err)
	}
	if diff := cmp.Diff("Running", cvd.Status); diff != "" {
		if err := common.DownloadHostBugReport(srv, groupName); err != nil {
			t.Errorf("failed creating bugreport: %s", err)
		}
		t.Fatalf("status mismatch (-want +got):\n%s", diff)
	}
	if err := adbH.Connect(cvd.ADBSerial); err != nil {
		t.Fatal(err)
	}
	if _, err = adbH.ExecShellCommand(cvd.ADBSerial, []string{"stat", tmpFile}); err != nil {
		t.Fatal(err)
	}

	// Verify delete snapshot logic.
	if err := verifyDeleteSnapshot(srv, groupName, cvd.Name, snapshotID); err != nil {
		t.Fatalf("delete snapshot check failed: %s", err)
	}
}

func createDevice(srv hoclient.HostOrchestratorClient, group_name, hostPkgDir, imageDir string) (*hoapi.CVD, error) {
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
        "graphics": {
          "gpu_mode": "guest_swiftshader"
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
		return nil, err
	}
	createReq := &hoapi.CreateCVDRequest{EnvConfig: envConfig}
	res, createErr := srv.CreateCVD(createReq, &hoclient.AccessTokenBuildAPICreds{})
	if createErr != nil {
		if err := common.DownloadHostBugReport(srv, group_name); err != nil {
			log.Printf("error downloading cvd bugreport: %v", err)
		}
		return nil, createErr
	}
	return res.CVDs[0], nil
}

func getCVD(srv hoclient.HostOrchestratorClient) (*hoapi.CVD, error) {
	cvds, err := srv.ListCVDs()
	if err != nil {
		return nil, err
	}
	if len(cvds) == 0 {
		return nil, errors.New("no cvds found")
	}
	return cvds[0], nil
}

func verifyDeleteSnapshot(client hoclient.HostOrchestratorClient, group, name, snapshotID string) error {
	// Delete snapshot
	if err := client.DeleteSnapshot(snapshotID); err != nil {
		return err
	}
	// Verifies the snapshot was deleted by aiming to restore from snapshot again.
	req := &hoapi.StartCVDRequest{SnapshotID: snapshotID}
	if err := client.Start(group, name, req); err != nil {
		var apiCallErr *hoclient.ApiCallError
		if errors.As(err, &apiCallErr) {
		} else {
			return fmt.Errorf("invalid error type: %T, expected %T", err, apiCallErr)
		}
		if diff := cmp.Diff(404, apiCallErr.HTTPStatusCode); diff != "" {
			return fmt.Errorf("http status code mismatch (-want +got):\n%s", diff)
		}
		part := fmt.Sprintf("%q not found", snapshotID)
		if !strings.Contains(apiCallErr.ErrorMsg.Error, part) {
			return fmt.Errorf("expected part %q in server error message %q", part, apiCallErr.ErrorMsg.Error)
		}
	}
	return nil
}
