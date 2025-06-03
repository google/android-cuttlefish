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
	"errors"
	"fmt"
	"log"
	"os/exec"
	"testing"

	"github.com/google/android-cuttlefish/e2etests/orchestration/common"

	hoapi "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	hoclient "github.com/google/android-cuttlefish/frontend/src/libhoclient"
)

const baseURL = "http://0.0.0.0:2080"

func TestPowerwash(t *testing.T) {
	srv := hoclient.NewHostOrchestratorClient(baseURL)
	t.Cleanup(func() {
		if err := common.CollectHOLogs(baseURL); err != nil {
			log.Printf("failed to collect HO logs: %s", err)
		}
	})
	uploadDir, err := srv.CreateUploadDir()
	if err != nil {
		t.Fatal(err)
	}
	cvd, err := createDevice(srv, uploadDir)
	if err != nil {
		t.Fatal(err)
	}
	adbBin := fmt.Sprintf("/var/lib/cuttlefish-common/user_artifacts/%s/bin/adb", uploadDir)
	adbH := &common.AdbHelper{Bin: adbBin}
	if err := adbH.StartServer(); err != nil {
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
	// Verify temp file was created
	_, err = adbH.ExecShellCommand(cvd.ADBSerial, []string{"stat", tmpFile})
	if err != nil {
		t.Fatal(err)
	}

	if err := srv.Powerwash(cvd.Group, cvd.Name); err != nil {
		t.Fatal(err)
	}

	// Verifies temporary file does not exist.
	_, err = adbH.ExecShellCommand(cvd.ADBSerial, []string{"stat", tmpFile})
	var exitCodeErr *exec.ExitError
	if !errors.As(err, &exitCodeErr) {
		t.Fatal(err)
	}
}

func createDevice(srv hoclient.HostOrchestratorClient, dir string) (*hoapi.CVD, error) {
	if err := common.UploadAndExtract(srv, dir, "../artifacts/images.zip"); err != nil {
		return nil, err
	}
	if err := common.UploadAndExtract(srv, dir, "../artifacts/cvd-host_package.tar.gz"); err != nil {
		return nil, err
	}
	return common.CreateCVDFromUserArtifactsDir(srv, dir)
}
