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
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"os"
	"os/exec"
	"path/filepath"
	"testing"

	"github.com/google/android-cuttlefish/e2etests/orchestration/common"

	hoapi "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	hoclient "github.com/google/android-cuttlefish/frontend/src/libhoclient"
	"github.com/google/go-cmp/cmp"
)

const markerContent = `symlinks: {
  target: "/usr/lib/cuttlefish-common/bin/powerbtn_cvd"
  link_name: "bin/powerbtn_cvd"
}

symlinks: {
  target: "/usr/lib/cuttlefish-common/etc/modem_simulator/files/numeric_operator.xml"
  link_name: "etc/modem_simulator/files/numeric_operator.xml"
}
`

func TestCreateInstance(t *testing.T) {
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
	if err := common.UploadAndExtract(srv, uploadDir, "../artifacts/images.zip"); err != nil {
		t.Fatal(err)
	}
	if err := modifyAndUploadCvdHostPackage(srv, uploadDir, "../artifacts/cvd-host_package.tar.gz"); err != nil {
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
        "disk": {
          "default_build": "@user_artifacts/` + uploadDir + `"
        }
      }
    ]
  }
  `
	envConfig := make(map[string]interface{})
	if err := json.Unmarshal([]byte(config), &envConfig); err != nil {
		t.Fatal(err)
	}
	createReq := &hoapi.CreateCVDRequest{EnvConfig: envConfig}

	got, createErr := srv.CreateCVD(createReq, &hoclient.AccessTokenBuildAPICreds{})

	if err := common.DownloadHostBugReport(srv, group_name); err != nil {
		t.Errorf("failed creating bugreport: %s", err)
	}
	if createErr != nil {
		t.Fatal(createErr)
	}
	if diff := cmp.Diff("Running", got.CVDs[0].Status); diff != "" {
		t.Errorf("status mismatch (-want +got):\n%s", diff)
	}
	dh, err := common.NewDockerHelper()
	if err != nil {
		t.Fatal(err)
	}
	dir := filepath.Join("/var/lib/cuttlefish-common/user_artifacts", uploadDir)
	link := filepath.Join(dir, "bin/adb")
	if err := dh.VerifySymlink(ctx.DockerContainerID, link); err == nil {
		t.Errorf("unexpected symlink: %s", link)
	}
	link = filepath.Join(dir, "bin/powerbtn_cvd")
	if err := dh.VerifySymlink(ctx.DockerContainerID, link); err != nil {
		t.Errorf("symlink not created: %s", err)
	}
	link = filepath.Join(dir, "etc/modem_simulator/files/numeric_operator.xml")
	if err := dh.VerifySymlink(ctx.DockerContainerID, link); err != nil {
		t.Errorf("symlink not created: %s", err)
	}
}

func modifyAndUploadCvdHostPackage(srv hoclient.HostOrchestratorService, uploadDir, src string) error {
	tmpDir, err := os.MkdirTemp("", "")
	if err != nil {
		return err
	}
	defer os.RemoveAll(tmpDir)
	markerPath := filepath.Join(tmpDir, "etc", "debian_substitution_marker")
	if err := os.Mkdir(filepath.Dir(markerPath), 0755); err != nil {
		return err
	}
	if err := os.WriteFile(markerPath, []byte(markerContent), 0640); err != nil {
		return err
	}
	dstTarGz := filepath.Join(tmpDir, "cvd-host_package.tar.gz")
	if err := runCmd("cp", src, dstTarGz); err != nil {
		return err
	}
	if err := runCmd("chmod", "640", dstTarGz); err != nil {
		return err
	}
	if err := runCmd("gzip", "-d", dstTarGz); err != nil {
		return err
	}
	dstTar := filepath.Join(tmpDir, "cvd-host_package.tar")
	if err := runCmd("tar", "--append", "--file", dstTar, "-C", tmpDir, "./etc"); err != nil {
		return err
	}
	if err := runCmd("gzip", dstTar); err != nil {
		return err
	}
	if err := srv.UploadFile(uploadDir, dstTarGz); err != nil {
		return err
	}
	op, err := srv.ExtractFile(uploadDir, "cvd-host_package.tar.gz")
	if err != nil {
		return err
	}
	if err := srv.WaitForOperation(op.Name, nil); err != nil {
		return err
	}
	return nil
}

func runCmd(name string, args ...string) error {
	cmd := exec.CommandContext(context.TODO(), name, args...)
	var stderr bytes.Buffer
	cmd.Stdout = nil
	cmd.Stderr = &stderr
	err := cmd.Run()
	if err != nil {
		return errors.New(stderr.String())
	}
	return nil
}
