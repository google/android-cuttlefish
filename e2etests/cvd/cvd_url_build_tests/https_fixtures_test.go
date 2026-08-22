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
	"archive/tar"
	"archive/zip"
	"compress/gzip"
	"encoding/json"
	"encoding/pem"
	"net/http"
	"net/http/httptest"
	"os"
	"path"
	"testing"
)

const hostPackageName = "cvd-host_package.tar.gz"

// An entry of `fetcher_config.json`.
type cvdFile struct {
	Source      string `json:"source"`
	BuildID     string `json:"build_id"`
	BuildTarget string `json:"build_target"`
}

type fetcherConfig struct {
	CvdFiles map[string]cvdFile `json:"cvd_files"`
}

// Serves dir over TLS and returns the base URL and the path of a PEM file
// holding the server certificate. The certificate is valid for 127.0.0.1, and
// `cvd` trusts it when the path is handed to it as `CURL_CA_BUNDLE`.
func serveArtifacts(t *testing.T, dir string) (string, string) {
	server := httptest.NewTLSServer(http.FileServer(http.Dir(dir)))
	t.Cleanup(server.Close)

	certpath := path.Join(t.TempDir(), "test_ca.pem")
	certfile, err := os.Create(certpath)
	if err != nil {
		t.Fatalf("failed to create %s: %v", certpath, err)
	}
	defer certfile.Close()

	block := pem.Block{Type: "CERTIFICATE", Bytes: server.Certificate().Raw}
	if err := pem.Encode(certfile, &block); err != nil {
		t.Fatalf("failed to write %s: %v", certpath, err)
	}

	return server.URL, certpath
}

func writeArtifact(t *testing.T, filepath string) {
	if err := os.WriteFile(filepath, []byte(path.Base(filepath)+"\n"), 0644); err != nil {
		t.Fatalf("failed to write %s: %v", filepath, err)
	}
}

func writeZip(t *testing.T, filepath string, members []string) {
	file, err := os.Create(filepath)
	if err != nil {
		t.Fatalf("failed to create %s: %v", filepath, err)
	}
	defer file.Close()

	writer := zip.NewWriter(file)
	for _, member := range members {
		entry, err := writer.Create(member)
		if err != nil {
			t.Fatalf("failed to add %s to %s: %v", member, filepath, err)
		}
		if _, err := entry.Write([]byte(member + "\n")); err != nil {
			t.Fatalf("failed to write %s in %s: %v", member, filepath, err)
		}
	}
	if err := writer.Close(); err != nil {
		t.Fatalf("failed to close %s: %v", filepath, err)
	}
}

func writeTarGz(t *testing.T, filepath string, members []string) {
	file, err := os.Create(filepath)
	if err != nil {
		t.Fatalf("failed to create %s: %v", filepath, err)
	}
	defer file.Close()

	compressor := gzip.NewWriter(file)
	writer := tar.NewWriter(compressor)
	for _, member := range members {
		contents := []byte(member + "\n")
		header := tar.Header{Name: member, Mode: 0755, Size: int64(len(contents))}
		if err := writer.WriteHeader(&header); err != nil {
			t.Fatalf("failed to add %s to %s: %v", member, filepath, err)
		}
		if _, err := writer.Write(contents); err != nil {
			t.Fatalf("failed to write %s in %s: %v", member, filepath, err)
		}
	}
	if err := writer.Close(); err != nil {
		t.Fatalf("failed to close the archive in %s: %v", filepath, err)
	}
	if err := compressor.Close(); err != nil {
		t.Fatalf("failed to close %s: %v", filepath, err)
	}
}

func readFile(t *testing.T, filepath string) string {
	contents, err := os.ReadFile(filepath)
	if err != nil {
		t.Fatalf("failed to read %s: %v", filepath, err)
	}
	return string(contents)
}

func readFetcherConfig(t *testing.T, directory string) fetcherConfig {
	filepath := path.Join(directory, "fetcher_config.json")
	var config fetcherConfig
	if err := json.Unmarshal([]byte(readFile(t, filepath)), &config); err != nil {
		t.Fatalf("failed to parse %s: %v", filepath, err)
	}
	return config
}

// Checks that `cvd fetch` recorded name as coming from the given URL build.
func checkProvenance(t *testing.T, config fetcherConfig, name string, source string, url string) {
	entry, ok := config.CvdFiles[name]
	if !ok {
		t.Fatalf("fetcher_config.json has no entry for %s", name)
	}
	if entry.Source != source {
		t.Errorf("%s source = %q, want %q", name, entry.Source, source)
	}
	if entry.BuildID != url {
		t.Errorf("%s build_id = %q, want %q", name, entry.BuildID, url)
	}
	if entry.BuildTarget != "url" {
		t.Errorf("%s build_target = %q, want \"url\"", name, entry.BuildTarget)
	}
}
