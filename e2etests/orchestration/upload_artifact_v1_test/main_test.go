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
	"path/filepath"
	"sync"
	"testing"

	hoclient "github.com/google/android-cuttlefish/frontend/src/libhoclient"
	"github.com/google/go-cmp/cmp"
)

const baseURL = "http://0.0.0.0:2080"

func TestUploadArtifact(t *testing.T) {
	srv := hoclient.NewHostOrchestratorService(baseURL)
	expected, err := hoclient.SHA256Checksum("../artifacts/cvd-host_package.tar.gz")
	if err != nil {
		t.Fatal(err)
	}
	_, err = srv.StatArtifact(expected)
	if err == nil {
		t.Fatal("expected error: file does not exist")
	}

	// Recreate two clients uploading the same artifact at the same time.
	var wg sync.WaitGroup
	for i := 0; i < 2; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			if err := srv.UploadArtifact("../artifacts/cvd-host_package.tar.gz"); err != nil {
				t.Error(err)
			}
			if _, err := srv.StatArtifact(expected); err != nil {
				t.Error(err)
			}
		}()
	}
	wg.Wait()

	got, err := hoclient.SHA256Checksum(filepath.Join("/var/lib/cuttlefish-common/user_artifacts", expected))
	if err != nil {
		t.Fatal(err)
	}
	if diff := cmp.Diff(expected, got); diff != "" {
		t.Fatalf("sha256sum mismatch (-want +got):\n%s", diff)
	}
}
