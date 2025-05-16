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
	"crypto/sha256"
	"fmt"
	"io"
	"mime/multipart"
	"net/http"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"testing"

	hoclient "github.com/google/android-cuttlefish/frontend/src/libhoclient"
	"github.com/google/go-cmp/cmp"
)

const baseURL = "http://0.0.0.0:2080"

// HO client does not use the `chunk_number`, `chunk_total`, `chunk_size_bytes`
// parameters, however the HO continues to support them for backwards
// compatibility reasons.
func TestUploadFileUseChunkNumber(t *testing.T) {
	srv := hoclient.NewHostOrchestratorService(baseURL)
	dir, err := srv.CreateUploadDir()
	if err != nil {
		t.Fatal(err)
	}
	localPath := "../artifacts/cvd-host_package.tar.gz"
	file, err := os.Open(localPath)
	if err != nil {
		t.Fatal(err)
	}
	defer file.Close()
	fileInfo, err := file.Stat()
	if err != nil {
		t.Fatal(err)
	}
	url := fmt.Sprintf("%s/userartifacts/%s", baseURL, dir)
	pipeReader, pipeWriter := io.Pipe()
	writer := multipart.NewWriter(pipeWriter)
	req, err := http.NewRequest(http.MethodPut, url, pipeReader)
	if err != nil {
		t.Fatal(err)
	}
	req.Header.Set("Content-Type", writer.FormDataContentType())
	go func() {
		defer pipeWriter.Close()
		defer writer.Close()
		if err := addFormField(writer, "chunk_number", strconv.Itoa(1)); err != nil {
			t.Log(err)
		}
		if err := addFormField(writer, "chunk_total", strconv.Itoa(1)); err != nil {
			t.Log(err)
		}
		if err := addFormField(writer, "chunk_size_bytes", strconv.FormatInt(fileInfo.Size(), 10)); err != nil {
			t.Log(err)
		}
		fw, err := writer.CreateFormFile("file", filepath.Base(localPath))
		if err != nil {
			t.Log(err)
		}
		if _, err = io.Copy(fw, file); err != nil {
			t.Log(err)
		}
	}()

	if _, err = http.DefaultClient.Do(req); err != nil {
		t.Fatal(err)
	}

	expected, err := hash(localPath)
	if err != nil {
		t.Fatal(err)
	}
	remotePath := fmt.Sprintf("/var/lib/cuttlefish-common/user_artifacts/%s/cvd-host_package.tar.gz", dir)
	got, err := hash(remotePath)
	if err != nil {
		t.Fatal(err)
	}
	if diff := cmp.Diff(expected, got); diff != "" {
		t.Fatalf("sha256sum mismatch (-want +got):\n%s", diff)
	}
}

func addFormField(writer *multipart.Writer, field, value string) error {
	fw, err := writer.CreateFormField(field)
	if err != nil {
		return err
	}
	_, err = io.Copy(fw, strings.NewReader(value))
	return err
}

func hash(filename string) (string, error) {
	f, err := os.Open(filename)
	if err != nil {
		return "", err
	}
	defer f.Close()
	h := sha256.New()
	if _, err := io.Copy(h, f); err != nil {
		return "", err
	}
	return fmt.Sprintf("%x", h.Sum(nil)), nil
}
