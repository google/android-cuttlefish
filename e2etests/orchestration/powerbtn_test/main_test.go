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
	"bufio"
	"errors"
	"fmt"
	"regexp"
	"strings"
	"testing"

	"github.com/google/android-cuttlefish/e2etests/orchestration/common"

	hoclient "github.com/google/android-cuttlefish/frontend/src/libhoclient"
	"github.com/google/go-cmp/cmp"
)

func TestPowerBtn(t *testing.T) {
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
	if err := common.UploadAndExtract(srv, uploadDir, "../artifacts/cvd-host_package.tar.gz"); err != nil {
		t.Fatal(err)
	}
	cvd, err := common.CreateCVDFromUserArtifactsDir(srv, uploadDir)
	if err != nil {
		t.Fatalf("failed creating instance: %s", err)
	}
	adbBin := fmt.Sprintf("/var/lib/cuttlefish-common/user_artifacts/%s/bin/adb", uploadDir)
	line, err := readScreenStateLine(ctx.DockerContainerID, adbBin, cvd.ADBSerial)
	if err != nil {
		t.Fatal(err)
	}
	if diff := cmp.Diff("mScreenState=ON", line); diff != "" {
		t.Errorf("response mismatch (-want +got):\n%s", diff)
	}

	if err := srv.Powerbtn(cvd.Group, cvd.Name); err != nil {
		t.Fatal(err)
	}

	line, err = readScreenStateLine(ctx.DockerContainerID, adbBin, cvd.ADBSerial)
	if err != nil {
		t.Fatal(err)
	}
	if diff := cmp.Diff("mScreenState=OFF", line); diff != "" {
		t.Errorf("response mismatch (-want +got):\n%s", diff)
	}
}

func readScreenStateLine(cID, adbBin, serial string) (string, error) {
	dh, err := common.NewDockerHelper()
	if err != nil {
		return "", err
	}
	if err := dh.StartADBServer(cID, adbBin); err != nil {
		return "", err
	}
	if err := dh.ConnectADB(cID, adbBin, serial); err != nil {
		return "", err
	}
	stdOut, err := dh.ExecADBShellCommand(cID, adbBin, serial, []string{"dumpsys", "display"})
	if err != nil {
		return "", err
	}
	re := regexp.MustCompile("^  mScreenState=[A-Z]+$")
	scanner := bufio.NewScanner(stdOut)
	for scanner.Scan() {
		line := scanner.Text()
		if re.MatchString(line) {
			return strings.TrimLeft(line, " "), nil
		}
	}
	return "", errors.New("`mScreenState=` line not found")
}
