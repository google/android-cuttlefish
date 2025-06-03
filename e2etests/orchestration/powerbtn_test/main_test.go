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
	"fmt"
	"log"
	"strings"
	"testing"
	"time"

	"github.com/google/android-cuttlefish/e2etests/orchestration/common"

	hoapi "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	hoclient "github.com/google/android-cuttlefish/frontend/src/libhoclient"
)

const baseURL = "http://0.0.0.0:2080"

func TestPowerBtn(t *testing.T) {
	srv := hoclient.NewHostOrchestratorClient(baseURL)
	uploadDir, err := srv.CreateUploadDir()
	if err != nil {
		t.Fatal(err)
	}
	cvd, err := createDevice(srv, uploadDir)
	if err != nil {
		t.Fatal(err)
	}
	// Monitor input devices events
	adbBin := fmt.Sprintf("/var/lib/cuttlefish-common/user_artifacts/%s/bin/adb", uploadDir)
	adbH := &common.AdbHelper{Bin: adbBin}
	if err := adbH.StartServer(); err != nil {
		t.Fatal(err)
	}
	if err := adbH.Connect(cvd.ADBSerial); err != nil {
		t.Fatal(err)
	}
	// Power button should generate at least 4 events.
	// EV_KEY       KEY_POWER            DOWN
	// EV_SYN       SYN_REPORT           00000000
	// EV_KEY       KEY_POWER            UP
	// EV_SYN       SYN_REPORT           00000000
	cmd := adbH.BuildShellCommand(cvd.ADBSerial, []string{"getevent", "-l", "-c 2", "/dev/input/event0"})
	stdoutBuff := &bytes.Buffer{}
	cmd.Stdout = stdoutBuff
	if err := cmd.Start(); err != nil {
		t.Fatal(err)
	}
	// Add some delay for `getevent` to properly start listening.
	time.Sleep(5 * time.Second)

	if err := srv.Powerbtn(cvd.Group, cvd.Name); err != nil {
		t.Fatal(err)
	}

	if err := cmd.Wait(); err != nil {
		t.Fatal(err)
	}
	stdoutStr := stdoutBuff.String()
	log.Printf("getevent stdout: %s", stdoutStr)
	const eventName = "KEY_POWER"
	if !strings.Contains(stdoutStr, eventName) {
		t.Errorf("event %q was not captured", eventName)
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
