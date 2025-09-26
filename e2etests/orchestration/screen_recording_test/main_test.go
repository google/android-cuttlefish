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
	"log"
	"testing"
	"time"

	"github.com/google/android-cuttlefish/e2etests/orchestration/common"

	hoclient "github.com/google/android-cuttlefish/frontend/src/libhoclient"
)

const baseURL = "http://0.0.0.0:2080"

type byteCounterWriter struct {
	Count int
}

func (bc *byteCounterWriter) Write(p []byte) (int, error) {
	n := len(p)
	bc.Count += n
	return n, nil
}

func TestScreenRecording(t *testing.T) {
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

	if recordings, err := srv.ListScreenRecordings(cvd.Group, cvd.Name); err != nil {
		t.Fatalf("Failed to list screen recordings: %v", err)
	} else if len(recordings) != 0 {
		t.Fatalf("Expected empty list of recordings, got: %v", recordings)
	}

	if err := srv.StartScreenRecording(cvd.Group, cvd.Name); err != nil {
		t.Fatalf("Failed to start recording: %v", err)
	}

	// Let it record something for a while
	time.Sleep(1 * time.Second)

	if err := srv.StopScreenRecording(cvd.Group, cvd.Name); err != nil {
		t.Fatalf("Failed to stop recording: %v", err)
	}

	recordings, err := srv.ListScreenRecordings(cvd.Group, cvd.Name)
	if err != nil {
		t.Fatalf("Failed to list screen recordings: %v", err)
	}
	if len(recordings) != 1 {
		t.Fatalf("Expected list of recordings with single element, got: %v", recordings)
	}

	byteCounter := byteCounterWriter{Count: 0}
	if err := srv.DownloadScreenRecording(cvd.Group, cvd.Name, recordings[0], &byteCounter); err != nil {
		t.Fatalf("Failed to download recording file: %v", err)
	}
	if byteCounter.Count == 0 {
		t.Fatal("Downloaded recording file is empty")
	}
}
