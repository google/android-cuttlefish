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
	"sync"
	"testing"
	"time"

	"github.com/google/android-cuttlefish/e2etests/orchestration/common"

	hoclient "github.com/google/android-cuttlefish/frontend/src/libhoclient"
)

const baseURL = "http://0.0.0.0:2080"

func TestUploadArtifact(t *testing.T) {
	client := hoclient.NewHostOrchestratorClient(baseURL)
	t.Cleanup(func() {
		if err := common.CollectHOLogs(baseURL); err != nil {
			log.Printf("failed to collect HO logs: %s", err)
		}
	})
	var uploadDuration time.Duration
	var uploadErr error
	wg := sync.WaitGroup{}
	wg.Add(1)
	go func() {
		defer wg.Done()
		start := time.Now()
		if err := client.UploadArtifact("../artifacts/images.zip"); err != nil {
			uploadErr = err
		}
		uploadDuration = time.Since(start)
		log.Printf("upload duration: %d ms", uploadDuration.Milliseconds())
	}()
	// upload a second time before the first upload is completed
	const secondRunnerWaitMs = 500
	time.Sleep(secondRunnerWaitMs * time.Millisecond)
	if err := client.UploadArtifact("../artifacts/images.zip"); err != nil {
		log.Fatal(err)
	}
	wg.Wait()
	if uploadErr != nil {
		log.Fatal(uploadErr)
	}
	if uploadDuration.Milliseconds() <= secondRunnerWaitMs {
		t.Error("second upload started after first upload was completed")
	}
	// upload a third time after the first upload was completed
	start := time.Now()
	if err := client.UploadArtifact("../artifacts/images.zip"); err != nil {
		t.Fatal(err)
	}
	elapsed := time.Since(start)
	if elapsed.Milliseconds() > uploadDuration.Milliseconds()/2.0 {
		t.Fatalf("expected shorter time for `UploadArtifact` because the file was already uploaded, got: %d ms, expected less than half of full duration: %d ms", elapsed.Milliseconds(), uploadDuration.Milliseconds()/2.0)
	}
}
