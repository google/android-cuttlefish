// Copyright (C) 2026 The Android Open Source Project
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
	_ "embed"
	"log"
	"slices"
	"testing"

	"github.com/google/android-cuttlefish/e2etests/orchestration/common"

	hoapi "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	hoclient "github.com/google/android-cuttlefish/frontend/src/libhoclient"
)

//go:embed events.bin
var events []byte

const baseURL = "http://0.0.0.0:2080"

func TestInjectInputEvents(t *testing.T) {
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

	devices, err := srv.ListEventDevices(cvd.Group, cvd.Name)
	if err != nil {
		t.Fatalf("Failed to list event devices: %v", err)
	}
	const deviceName = "keyboard"
	if !slices.Contains(devices, hoapi.EventDevice{Name: deviceName}) {
		t.Fatalf("Device %q not found in event devices list: %v", deviceName, devices)
	}

	if err := srv.InjectInputEvents(cvd.Group, cvd.Name, deviceName, bytes.NewReader(events)); err != nil {
		t.Fatalf("Failed to inject input events: %v", err)
	}
}
