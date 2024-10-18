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
	"net/http"
	"testing"

	"github.com/google/android-cuttlefish/e2etests/orchestration/common"

	hoapi "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	hoclient "github.com/google/android-cuttlefish/frontend/src/libhoclient"
	"github.com/google/go-cmp/cmp"
)

func TestSnapshot(t *testing.T) {
	ctx, err := common.Setup(61003)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() {
		common.Cleanup(ctx)
	})
	dh, err := common.NewDockerHelper()
	if err != nil {
		t.Fatal(err)
	}
	srv := hoclient.NewHostOrchestratorService(ctx.ServiceURL)
	uploadDir, err := uploadArtifacts(srv)
	if err != nil {
		t.Fatal(err)
	}
	const groupName = "cvd"
	cvd, err := createDevice(srv, groupName, uploadDir)
	if err != nil {
		t.Fatal(err)
	}
	cID := ctx.DockerContainerID
	adbBin := fmt.Sprintf("/var/lib/cuttlefish-common/user_artifacts/%s/bin/adb", uploadDir)
	// adb connect
	if err := dh.StartADBServer(cID, adbBin); err != nil {
		t.Fatal(err)
	}
	if err := dh.ConnectADB(cID, adbBin, cvd.ADBSerial); err != nil {
		t.Fatal(err)
	}
	const tmpFile = "/data/local/tmp/foo"
	// Create temporary file
	if err := dh.ExecADBShellCommand(cID, adbBin, cvd.ADBSerial, []string{"touch", tmpFile}); err != nil {
		t.Fatal(err)
	}
	// Create a snapshot containing the temporary file.
	createSnapshotRes, err := createSnapshot(ctx.ServiceURL, groupName, cvd.Name)
	if err != nil {
		t.Fatal(err)
	}
	// Powerwash the device removing the temporary file.
	if err := powerwash(ctx.ServiceURL, groupName, cvd.Name); err != nil {
		t.Fatal(err)
	}
	// Verifies temporary file does not exist.
	err = dh.ExecADBShellCommand(cID, adbBin, cvd.ADBSerial, []string{"stat", tmpFile})
	var exitCodeErr *common.DockerExecExitCodeError
	if !errors.As(err, &exitCodeErr) {
		t.Fatal(err)
	}
	// Stop the device.
	if err := stopDevice(ctx.ServiceURL, groupName, cvd.Name); err != nil {
		t.Fatal(err)
	}
	// Restore the device from the snapshot.
	if err := startDevice(ctx.ServiceURL, groupName, cvd.Name, createSnapshotRes.SnapshotID); err != nil {
		if err := common.DownloadHostBugReport(srv, groupName); err != nil {
			t.Errorf("failed creating bugreport: %s", err)
		}
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
	if err := dh.ConnectADB(cID, adbBin, cvd.ADBSerial); err != nil {
		t.Fatal(err)
	}
	if err := dh.ExecADBShellCommand(cID, adbBin, cvd.ADBSerial, []string{"stat", tmpFile}); err != nil {
		t.Fatal(err)
	}
}

func uploadArtifacts(srv hoclient.HostOrchestratorService) (string, error) {
	uploadDir, err := srv.CreateUploadDir()
	if err != nil {
		return "", err
	}
	if err := common.UploadAndExtract(srv, uploadDir, "../artifacts/images.zip"); err != nil {
		return "", err
	}
	if err := common.UploadAndExtract(srv, uploadDir, "../artifacts/cvd-host_package.tar.gz"); err != nil {
		return "", err
	}
	return uploadDir, nil
}

func createDevice(srv hoclient.HostOrchestratorService, group_name, artifactsDir string) (*hoapi.CVD, error) {
	config := `
  {
    "common": {
      "group_name": "` + group_name + `",
      "host_package": "@user_artifacts/` + artifactsDir + `"
    },
    "instances": [
      {
        "vm": {
          "memory_mb": 8192,
          "setupwizard_mode": "OPTIONAL",
          "cpus": 8
        },
        "disk": {
          "default_build": "@user_artifacts/` + artifactsDir + `"
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
	res, createErr := srv.CreateCVD(createReq, hoclient.BuildAPICredential{})
	if createErr != nil {
		if err := common.DownloadHostBugReport(srv, group_name); err != nil {
			log.Printf("error downloading cvd bugreport: %v", err)
		}
		return nil, createErr
	}
	return res.CVDs[0], nil
}

// TODO(b/370552105): Use HO API objects definitions from HEAD in e2e tests.
type CreateSnapshotResponse struct {
	SnapshotID string `json:"snapshot_id"`
}

// TODO(b/370552105): Use HO API objects definitions from HEAD in e2e tests.
type StartCVDRequest struct {
	// Start from the relevant snaphost if not empty.
	SnapshotID string `json:"snapshot_id,omitempty"`
}

// TODO(b/370550070) Remove once this method is added to the client implementation.
func createSnapshot(srvURL, group, name string) (*CreateSnapshotResponse, error) {
	helper := hoclient.HTTPHelper{
		Client:       http.DefaultClient,
		RootEndpoint: srvURL,
	}
	op := &hoapi.Operation{}
	path := fmt.Sprintf("/cvds/%s/%s/snapshots", group, name)
	rb := helper.NewPostRequest(path, nil)
	if err := rb.JSONResDo(op); err != nil {
		return nil, err
	}
	srv := hoclient.NewHostOrchestratorService(srvURL)
	res := &CreateSnapshotResponse{}
	if err := srv.WaitForOperation(op.Name, res); err != nil {
		return nil, err
	}
	return res, nil
}

func powerwash(srvURL, group, name string) error {
	return doRequest(srvURL, group, name, "powerwash", nil)
}

func stopDevice(srvURL, group, name string) error {
	return doRequest(srvURL, group, name, "stop", nil)
}

func startDevice(srvURL, group, name, snapshotID string) error {
	body := &StartCVDRequest{SnapshotID: snapshotID}
	return doRequest(srvURL, group, name, "start", body)
}

func doRequest(srvURL, group, name, oper string, body any) error {
	helper := hoclient.HTTPHelper{
		Client:       http.DefaultClient,
		RootEndpoint: srvURL,
	}
	op := &hoapi.Operation{}
	path := fmt.Sprintf("/cvds/%s/%s/:%s", group, name, oper)
	rb := helper.NewPostRequest(path, body)
	if err := rb.JSONResDo(op); err != nil {
		return err
	}
	srv := hoclient.NewHostOrchestratorService(srvURL)
	res := &hoapi.EmptyResponse{}
	if err := srv.WaitForOperation(op.Name, &res); err != nil {
		return err
	}
	return nil
}

func getCVD(srv hoclient.HostOrchestratorService) (*hoapi.CVD, error) {
	cvds, err := srv.ListCVDs()
	if err != nil {
		return nil, err
	}
	if len(cvds) == 0 {
		return nil, errors.New("no cvds found")
	}
	return cvds[0], nil
}
