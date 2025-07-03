// Copyright 2022 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package libhoclient

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"testing"
	"time"

	hoapi "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	"github.com/google/go-cmp/cmp"
)

func TestUploadFileChunkSizeBytesIsZeroPanic(t *testing.T) {
	defer func() {
		if r := recover(); r == nil {
			t.Errorf("did not panic")
		}
	}()

	srv := NewHostOrchestratorClient("https://test.com")

	srv.UploadFileWithOptions("dir", "baz", UploadOptions{ChunkSizeBytes: 0})
}

func TestUploadFileExponentialBackoff(t *testing.T) {
	tempDir := createTempDir(t)
	defer os.RemoveAll(tempDir)
	waldoFile := createTempFile(t, tempDir, "waldo", []byte("l"))
	timestamps := make([]time.Time, 0)
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		timestamps = append(timestamps, time.Now())
		if len(timestamps) < 3 {
			writeErr(w, 500)
			return
		}
		writeOK(w, struct{}{})
	}))
	defer ts.Close()

	srv := NewHostOrchestratorClient(ts.URL)

	err := srv.UploadFileWithOptions("dir", waldoFile, UploadOptions{
		BackOffOpts: ExpBackOffOptions{
			InitialDuration: 100 * time.Millisecond,
			Multiplier:      2,
			MaxElapsedTime:  1 * time.Second,
		},
		ChunkSizeBytes: 2,
		NumWorkers:     10,
	})

	if err != nil {
		t.Fatal(err)
	}
	if timestamps[1].Sub(timestamps[0]) < 100*time.Millisecond {
		t.Fatal("first retry shouldn't be in less than 100ms")
	}
	if timestamps[2].Sub(timestamps[1]) < 200*time.Millisecond {
		t.Fatal("second retry shouldn't be in less than 200ms")
	}
}

func TestUploadFileExponentialBackoffReachedElapsedTime(t *testing.T) {
	tempDir := createTempDir(t)
	defer os.RemoveAll(tempDir)
	waldoFile := createTempFile(t, tempDir, "waldo", []byte("l"))
	attempts := 0
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		attempts = attempts + 1
		writeErr(w, 500)
	}))
	defer ts.Close()

	srv := NewHostOrchestratorClient(ts.URL)

	err := srv.UploadFileWithOptions("dir", waldoFile, UploadOptions{
		BackOffOpts: ExpBackOffOptions{
			InitialDuration:     100 * time.Millisecond,
			RandomizationFactor: 0.5,
			Multiplier:          2,
			MaxElapsedTime:      1 * time.Second,
		},
		ChunkSizeBytes: 2,
		NumWorkers:     10,
	})

	if err == nil {
		t.Fatal("expected error")
	}
	if attempts == 0 {
		t.Fatal("server was never reached")
	}
}

func TestCreateCVD(t *testing.T) {
	fakeRes := &hoapi.CreateCVDResponse{CVDs: []*hoapi.CVD{{Name: "1"}}}
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch ep := r.Method + " " + r.URL.Path; ep {
		case "POST /cvds":
			writeOK(w, hoapi.Operation{Name: "foo"})
		case "POST /operations/foo/:wait":
			writeOK(w, fakeRes)
		default:
			t.Fatal("unexpected endpoint: " + ep)
		}
	}))
	defer ts.Close()
	srv := NewHostOrchestratorClient(ts.URL)
	req := &hoapi.CreateCVDRequest{EnvConfig: map[string]interface{}{}}

	res, err := srv.CreateCVD(req, &AccessTokenBuildAPICreds{})

	if err != nil {
		t.Fatal(err)
	}
	if diff := cmp.Diff(fakeRes, res); diff != "" {
		t.Fatalf("response mismatch (-want +got):\n%s", diff)
	}
}

func TestUploadArtifactAlreadyExist(t *testing.T) {
	tempDir := createTempDir(t)
	defer os.RemoveAll(tempDir)
	testFile := createTempFile(t, tempDir, "waldo", []byte("waldo"))
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch ep := r.Method + " " + r.URL.Path; ep {
		case "GET /v1/userartifacts/d2c055002a6cdf8dd9edf90c7a666cb5f7f2d25da8519ec206f56777d74e0c7d":
			writeOK(w, hoapi.StatArtifactResponse{})
		default:
			t.Fatal("unexpected endpoint: " + ep)
		}
	}))
	defer ts.Close()
	client := NewHostOrchestratorClient(ts.URL)

	if err := client.UploadArtifact(testFile); err != nil {
		t.Fatal(err)
	}
}

func TestUploadArtifactReceive409WhenUploadingChunks(t *testing.T) {
	tempDir := createTempDir(t)
	defer os.RemoveAll(tempDir)
	testFile := createTempFile(t, tempDir, "waldo", []byte("waldo"))
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch ep := r.Method + " " + r.URL.Path; ep {
		case "GET /v1/userartifacts/d2c055002a6cdf8dd9edf90c7a666cb5f7f2d25da8519ec206f56777d74e0c7d":
			writeErr(w, http.StatusNotFound)
		case "PUT /v1/userartifacts/d2c055002a6cdf8dd9edf90c7a666cb5f7f2d25da8519ec206f56777d74e0c7d":
			writeErr(w, http.StatusConflict)
		default:
			t.Fatal("unexpected endpoint: " + ep)
		}
	}))
	defer ts.Close()
	client := NewHostOrchestratorClient(ts.URL)

	if err := client.UploadArtifact(testFile); err != nil {
		t.Fatal(err)
	}
}

func TestExtractArtifactReceive404WhenArtifactDoesNotExist(t *testing.T) {
	tempDir := createTempDir(t)
	defer os.RemoveAll(tempDir)
	testFile := createTempFile(t, tempDir, "waldo", []byte("waldo"))
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch ep := r.Method + " " + r.URL.Path; ep {
		case "GET /v1/userartifacts/d2c055002a6cdf8dd9edf90c7a666cb5f7f2d25da8519ec206f56777d74e0c7d":
			writeErr(w, http.StatusNotFound)
		default:
			t.Fatal("unexpected endpoint: " + ep)
		}
	}))
	defer ts.Close()
	client := NewHostOrchestratorClient(ts.URL)

	if _, err := client.ExtractArtifact(testFile); err == nil {
		t.Fatal("Expected error")
	}
}

func TestExtractArtifactSucceeds(t *testing.T) {
	tempDir := createTempDir(t)
	defer os.RemoveAll(tempDir)
	testFile := createTempFile(t, tempDir, "waldo", []byte("waldo"))
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch ep := r.Method + " " + r.URL.Path; ep {
		case "GET /v1/userartifacts/d2c055002a6cdf8dd9edf90c7a666cb5f7f2d25da8519ec206f56777d74e0c7d":
			writeOK(w, hoapi.StatArtifactResponse{})
		case "POST /v1/userartifacts/d2c055002a6cdf8dd9edf90c7a666cb5f7f2d25da8519ec206f56777d74e0c7d/:extract":
			writeOK(w, hoapi.Operation{Name: "foo"})
		default:
			t.Fatal("unexpected endpoint: " + ep)
		}
	}))
	defer ts.Close()
	client := NewHostOrchestratorClient(ts.URL)

	expected := &hoapi.Operation{Name: "foo"}
	if op, err := client.ExtractArtifact(testFile); err != nil {
		t.Fatal(err)
	} else if diff := cmp.Diff(expected, op); diff != "" {
		t.Fatalf("response mismatch (-want +got):\n%s", diff)
	}
}

func TestCreateCVDWithUserProjectOverride(t *testing.T) {
	fakeRes := &hoapi.CreateCVDResponse{CVDs: []*hoapi.CVD{{Name: "1"}}}
	token := "foo"
	projectID := "fake-project"
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch ep := r.Method + " " + r.URL.Path; ep {
		case "POST /cvds":
			if r.Header.Get(HTTPHeaderBuildAPICreds) != token {
				t.Fatal("unexpected access token: " + r.Header.Get(HTTPHeaderBuildAPICreds))
			}
			if r.Header.Get(HTTPHeaderBuildAPICredsUserProjectID) != projectID {
				t.Fatal("unexpected user project id: " + r.Header.Get(HTTPHeaderBuildAPICredsUserProjectID))
			}
			writeOK(w, hoapi.Operation{Name: "foo"})
		case "POST /operations/foo/:wait":
			writeOK(w, fakeRes)
		default:
			t.Fatal("unexpected endpoint: " + ep)
		}
	}))
	defer ts.Close()
	srv := NewHostOrchestratorClient(ts.URL)
	req := &hoapi.CreateCVDRequest{EnvConfig: map[string]interface{}{}}

	res, err := srv.CreateCVD(req, &AccessTokenBuildAPICreds{AccessToken: token, UserProjectID: projectID})

	if err != nil {
		t.Fatal(err)
	}
	if diff := cmp.Diff(fakeRes, res); diff != "" {
		t.Fatalf("response mismatch (-want +got):\n%s", diff)
	}
}

func createTempDir(t *testing.T) string {
	dir, err := os.MkdirTemp("", "cvdrTest")
	if err != nil {
		t.Fatal(err)
	}
	return dir
}

func createTempFile(t *testing.T, dir, name string, content []byte) string {
	file := filepath.Join(dir, name)
	if err := os.WriteFile(file, content, 0666); err != nil {
		t.Fatal(err)
	}
	return file
}

func writeErr(w http.ResponseWriter, statusCode int) {
	write(w, &struct{ StatusCode int }{StatusCode: statusCode}, statusCode)
}

func writeOK(w http.ResponseWriter, data any) {
	write(w, data, http.StatusOK)
}

func write(w http.ResponseWriter, data any, statusCode int) {
	w.WriteHeader(statusCode)
	w.Header().Set("Content-Type", "application/json")
	encoder := json.NewEncoder(w)
	encoder.Encode(data)
}
