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

package client

import (
	"fmt"
	"io"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"sync"
	"testing"
	"time"

	hoapi "github.com/google/android-cuttlefish/frontend/src/liboperator/api/v1"

	"github.com/google/go-cmp/cmp"
)

func TestUploadFilesChunkSizeBytesIsZeroPanic(t *testing.T) {
	defer func() {
		if r := recover(); r == nil {
			t.Errorf("did not panic")
		}
	}()
	opts := &ServiceOptions{
		RootEndpoint: "https://test.com",
		DumpOut:      io.Discard,
	}
	srv, _ := NewService(opts)

	srv.HostService("foo").UploadFilesWithOptions("bar", []string{"baz"}, UploadOptions{ChunkSizeBytes: 0})
}

func TestUploadFilesSucceeds(t *testing.T) {
	host := "foo"
	uploadDir := "bar"
	tempDir := createTempDir(t)
	defer os.RemoveAll(tempDir)
	quxFile := createTempFile(t, tempDir, "qux", []byte("lorem"))
	waldoFile := createTempFile(t, tempDir, "waldo", []byte("l"))
	xyzzyFile := createTempFile(t, tempDir, "xyzzy", []byte("abraca"))
	mu := sync.Mutex{}
	// expected uploads are keyed by format %filename %chunknumber of %chunktotal with the chunk content as value
	uploads := map[string]struct{ Content []byte }{
		// qux
		"qux 1 of 3": {Content: []byte("lo")},
		"qux 2 of 3": {Content: []byte("re")},
		"qux 3 of 3": {Content: []byte("m")},
		// waldo
		"waldo 1 of 1": {Content: []byte("l")},
		// xyzzy
		"xyzzy 1 of 3": {Content: []byte("ab")},
		"xyzzy 2 of 3": {Content: []byte("ra")},
		"xyzzy 3 of 3": {Content: []byte("ca")},
	}
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		mu.Lock()
		defer mu.Unlock()
		switch ep := r.Method + " " + r.URL.Path; ep {
		case "PUT /hosts/" + host + "/userartifacts/" + uploadDir:
			chunkNumber := r.PostFormValue("chunk_number")
			chunkTotal := r.PostFormValue("chunk_total")
			f, fheader, err := r.FormFile("file")
			if err != nil {
				t.Fatal(err)
			}
			expectedUploadKey := fmt.Sprintf("%s %s of %s", fheader.Filename, chunkNumber, chunkTotal)
			val, ok := uploads[expectedUploadKey]
			if !ok {
				t.Fatalf("unexpected upload with filename: %q, chunk number: %s, chunk total: %s",
					fheader.Filename, chunkNumber, chunkTotal)
			}
			delete(uploads, expectedUploadKey)
			b, err := io.ReadAll(f)
			if err != nil {
				t.Fatal(err)
			}
			if diff := cmp.Diff(val.Content, b); diff != "" {
				t.Fatalf("chunk content mismatch %q (-want +got):\n%s", fheader.Filename, diff)
			}
			writeOK(w, struct{}{})
		default:
			t.Fatal("unexpected endpoint: " + ep)
		}
	}))
	defer ts.Close()
	opts := &ServiceOptions{
		RootEndpoint:   ts.URL,
		DumpOut:        io.Discard,
		ChunkSizeBytes: 2,
	}
	srv, _ := NewService(opts)

	err := srv.HostService(host).UploadFilesWithOptions(uploadDir, []string{quxFile, waldoFile, xyzzyFile},
		UploadOptions{
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

	if len(uploads) != 0 {
		t.Errorf("missing chunk uploads:  %v", uploads)
	}
}

func TestUploadFilesExponentialBackoff(t *testing.T) {
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
	opts := &ServiceOptions{
		RootEndpoint:   ts.URL,
		DumpOut:        io.Discard,
		ChunkSizeBytes: 2,
	}
	srv, _ := NewService(opts)

	err := srv.HostService("foo").UploadFilesWithOptions("bar", []string{waldoFile}, UploadOptions{
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

func TestUploadFilesExponentialBackoffReachedElapsedTime(t *testing.T) {
	tempDir := createTempDir(t)
	defer os.RemoveAll(tempDir)
	waldoFile := createTempFile(t, tempDir, "waldo", []byte("l"))
	attempts := 0
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		attempts = attempts + 1
		writeErr(w, 500)
	}))
	defer ts.Close()
	opts := &ServiceOptions{
		RootEndpoint:   ts.URL,
		DumpOut:        io.Discard,
		ChunkSizeBytes: 2,
	}
	srv, _ := NewService(opts)

	err := srv.HostService("foo").UploadFilesWithOptions("bar", []string{waldoFile}, UploadOptions{
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
	srv := NewHostOrchestratorService(ts.URL)
	req := &hoapi.CreateCVDRequest{EnvConfig: map[string]interface{}{}}

	res, err := srv.CreateCVD(req, "")

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
