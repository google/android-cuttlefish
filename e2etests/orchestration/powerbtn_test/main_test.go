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
	"time"

	"github.com/google/android-cuttlefish/e2etests/orchestration/common"

	hoapi "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	hoclient "github.com/google/android-cuttlefish/frontend/src/libhoclient"
	"github.com/google/go-cmp/cmp"
)

const baseURL = "http://0.0.0.0:2080"

func TestPowerBtn(t *testing.T) {
	srv := hoclient.NewHostOrchestratorService(baseURL)
	uploadDir, err := srv.CreateUploadDir()
	if err != nil {
		t.Fatal(err)
	}
	cvd, err := createDevice(srv, uploadDir)
	if err != nil {
		t.Fatal(err)
	}
	adbBin := fmt.Sprintf("/var/lib/cuttlefish-common/user_artifacts/%s/bin/adb", uploadDir)
	line, err := readScreenStateLine(adbBin, cvd.ADBSerial)
	if err != nil {
		t.Fatal(err)
	}
	if diff := cmp.Diff("mScreenState=ON", line); diff != "" {
		t.Errorf("config mismatch (-want +got):\n%s", diff)
	}

	if err := srv.Powerbtn(cvd.Group, cvd.Name); err != nil {
		t.Fatal(err)
	}

	screenOff := false
	// The change is not reflected in `dumpsys display` right away.
	for attempt := 0; attempt < 3 && !screenOff; attempt++ {
		time.Sleep(1 * time.Second)
		line, err = readScreenStateLine(adbBin, cvd.ADBSerial)
		if err != nil {
			t.Fatal(err)
		}
		screenOff = (line == "mScreenState=OFF")
	}
	if !screenOff {
		t.Error("`mScreenState=OFF` never found in `dumpsys display`")
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

func readScreenStateLine(adbBin, serial string) (string, error) {
	adbH := &common.AdbHelper{Bin: adbBin}
	if err := adbH.StartServer(); err != nil {
		return "", fmt.Errorf("adb start-server failed: %w", err)
	}
	if err := adbH.Connect(serial); err != nil {
		return "", fmt.Errorf("adb connect with serial %q failed: %w", serial, err)
	}
	stdoutStderr, err := adbH.ExecShellCommand(serial, []string{"dumpsys", "display"})
	if err != nil {
		return "", fmt.Errorf("adb shell with serial %q failed: %w", serial, err)
	}
	re := regexp.MustCompile("^  mScreenState=[A-Z]+$")
	scanner := bufio.NewScanner(strings.NewReader(stdoutStderr))
	for scanner.Scan() {
		line := scanner.Text()
		if re.MatchString(line) {
			return strings.TrimLeft(line, " "), nil
		}
	}
	return "", errors.New("`mScreenState=` line not found")
}
