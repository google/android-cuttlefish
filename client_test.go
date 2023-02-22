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
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"regexp"
	"sync"
	"testing"
	"time"

	apiv1 "github.com/google/cloud-android-orchestration/api/v1"

	"github.com/google/go-cmp/cmp"
	"github.com/hashicorp/go-multierror"
)

func TestRetryLogic(t *testing.T) {
	failsTotal := 2
	failsCounter := 0
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		opName := "op-foo"
		switch ep := r.Method + " " + r.URL.Path; ep {
		case "POST /hosts":
			writeOK(w, &apiv1.Operation{Name: opName})
		case "POST /operations/" + opName + "/:wait":
			if failsCounter < failsTotal {
				failsCounter++
				writeErr(w, 503)
				return
			}
			writeOK(w, &apiv1.HostInstance{Name: "foo"})
		default:
			t.Fatal("unexpected endpoint: " + ep)
		}

	}))
	defer ts.Close()
	opts := &ServiceOptions{
		RootEndpoint:  ts.URL,
		DumpOut:       io.Discard,
		RetryAttempts: 2,
		RetryDelay:    100 * time.Millisecond,
	}
	client, _ := NewService(opts)

	start := time.Now()
	host, _ := client.CreateHost(&apiv1.CreateHostRequest{})
	duration := time.Since(start)

	expected := &apiv1.HostInstance{Name: "foo"}
	if diff := cmp.Diff(expected, host); diff != "" {
		t.Errorf("host instance mismatch (-want +got):\n%s", diff)
	}
	if duration < opts.RetryDelay*2 {
		t.Error("duration faster than expected")
	}
}

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

	srv.UploadFiles("foo", "bar", []string{"baz"})
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

	err := srv.UploadFiles(host, uploadDir, []string{quxFile, waldoFile, xyzzyFile})
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
		ChunkUploadBackOffOpts: BackOffOpts{
			InitialDuration: 100 * time.Millisecond,
			Multiplier:      2,
			MaxElapsedTime:  1 * time.Minute,
		},
	}
	srv, _ := NewService(opts)

	err := srv.UploadFiles("foo", "bar", []string{waldoFile})

	if err != nil {
		t.Fatal(err)
	}
	if timestamps[1].Sub(timestamps[0]) < 100*time.Millisecond {
		t.Fatal("first retry shouldn't be in less than 100ms")
	}
	if timestamps[2].Sub(timestamps[1]) < 200*time.Millisecond {
		t.Fatal("first retry shouldn't be in less than 200ms")
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
		ChunkUploadBackOffOpts: BackOffOpts{
			InitialDuration: 100 * time.Millisecond,
			Multiplier:      2,
			MaxElapsedTime:  1 * time.Second,
		},
	}
	srv, _ := NewService(opts)

	err := srv.UploadFiles("foo", "bar", []string{waldoFile})

	if err == nil {
		t.Fatal("expected error")
	}
	if attempts == 0 {
		t.Fatal("server was never reached")
	}
}

func TestDeleteHosts(t *testing.T) {
	existingNames := map[string]struct{}{"bar": {}, "baz": {}}
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Method != "DELETE" {
			panic("unexpected method: " + r.Method)
		}
		re := regexp.MustCompile(`^/hosts/(.*)$`)
		matches := re.FindStringSubmatch(r.URL.Path)
		if len(matches) != 2 {
			panic("unexpected path: " + r.URL.Path)
		}
		if _, ok := existingNames[matches[1]]; ok {
			writeOK(w, "")
		} else {
			writeErr(w, 404)
		}
	}))
	defer ts.Close()
	opts := &ServiceOptions{
		RootEndpoint: ts.URL,
		DumpOut:      io.Discard,
	}
	srv, _ := NewService(opts)

	err := srv.DeleteHosts([]string{"foo", "bar", "baz", "quz"})

	merr, _ := err.(*multierror.Error)
	errs := merr.WrappedErrors()
	if len(errs) != 2 {
		t.Errorf("expected 2, got: %d", len(errs))
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
	write(w, &apiv1.Error{Code: statusCode}, statusCode)
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
