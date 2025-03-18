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
	"testing"

	"github.com/google/android-cuttlefish/e2etests/orchestration/common"

	hoapi "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	hoclient "github.com/google/android-cuttlefish/frontend/src/libhoclient"
)

func TestPowerwash(t *testing.T) {
	ctx, err := common.Setup()
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
	cvd, err := createDevice(srv, uploadDir)
	if err != nil {
		t.Fatal(err)
	}
	dh, err := common.NewDockerHelper()
	if err != nil {
		t.Fatal(err)
	}
	cID := ctx.DockerContainerID
	adbBin := fmt.Sprintf("/var/lib/cuttlefish-common/user_artifacts/%s/bin/adb", uploadDir)
	if err := dh.StartADBServer(cID, adbBin); err != nil {
		t.Fatal(err)
	}
	if err := dh.ConnectADB(cID, adbBin, cvd.ADBSerial); err != nil {
		t.Fatal(err)
	}
	const tmpFile = "/data/local/tmp/foo"
	// Create temporary file
	if _, err := dh.ExecADBShellCommand(cID, adbBin, cvd.ADBSerial, []string{"touch", tmpFile}); err != nil {
		t.Fatal(err)
	}
	// Verify temp file was created
	_, err = dh.ExecADBShellCommand(cID, adbBin, cvd.ADBSerial, []string{"stat", tmpFile})
	if err != nil {
		t.Fatal(err)
	}
	// Make sure `powerwash_cvd` is not being used.
	toolBin := fmt.Sprintf("/var/lib/cuttlefish-common/user_artifacts/%s/bin/powerwash_cvd", uploadDir)
	if err := dh.RemoveHostTool(cID, toolBin); err != nil {
		t.Fatal(err)
	}

	if err := srv.Powerwash(cvd.Group, cvd.Name); err != nil {
		t.Fatal(err)
	}

	// Verifies temporary file does not exist.
	_, err = dh.ExecADBShellCommand(cID, adbBin, cvd.ADBSerial, []string{"stat", tmpFile})
	if err == nil {
		t.Fatalf("temp file %q still exists after powerwash", tmpFile)
	} else {
		var exitCodeErr *common.DockerExecExitCodeError
		if !errors.As(err, &exitCodeErr) {
			t.Fatal(err)
		}
	}
}

func createDevice(srv hoclient.HostOrchestratorService, dir string) (*hoapi.CVD, error) {
	if err := common.UploadAndExtract(srv, dir, "../artifacts/images.zip"); err != nil {
		return nil, err
	}
	if err := common.UploadAndExtract(srv, dir, "../artifacts/cvd-host_package.tar.gz"); err != nil {
		return nil, err
	}
	return common.CreateCVDFromUserArtifactsDir(srv, dir)
}
