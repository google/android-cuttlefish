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
	"log"
	"os/exec"
	"testing"

	"github.com/google/android-cuttlefish/e2etests/orchestration/common"

	hoclient "github.com/google/android-cuttlefish/frontend/src/libhoclient"
)

const baseURL = "http://0.0.0.0:2080"

func TestPowerwash(t *testing.T) {
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
	cvd, err := common.CreateCVDFromImageDirs(srv, hostPkgDir, imageDir)
	if err != nil {
		t.Fatal(err)
	}
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
