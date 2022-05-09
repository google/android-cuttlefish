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

package main

import (
	"bytes"
	"errors"
	"fmt"
	"io"
	"io/ioutil"
	"net/http"
	"net/http/httptest"
	"os"
	"strings"
	"testing"
)

func TestCreateCVDInvalidRequests(t *testing.T) {
	im := &InstanceManager{}
	var validRequest = func() *CreateCVDRequest {
		return &CreateCVDRequest{
			BuildInfo: &BuildInfo{
				BuildID: "1234",
				Target:  "aosp_cf_x86_64_phone-userdebug",
			},
			FetchCVDBuildID: "9999",
		}
	}
	// Make sure the valid request is indeed valid.
	if err := validateRequest(validRequest()); err != nil {
		t.Fatalf("the valid request is not valid")
	}
	var tests = []struct {
		corruptRequest func(r *CreateCVDRequest)
	}{
		{func(r *CreateCVDRequest) { r.BuildInfo = nil }},
		{func(r *CreateCVDRequest) { r.BuildInfo.BuildID = "" }},
		{func(r *CreateCVDRequest) { r.BuildInfo.Target = "" }},
		{func(r *CreateCVDRequest) { r.FetchCVDBuildID = "" }},
	}

	for _, test := range tests {
		req := validRequest()
		test.corruptRequest(req)
		_, err := im.CreateCVD(req)
		var appErr *AppError
		if !errors.As(err, &appErr) {
			t.Errorf("unexpected error <<\"%v\">>, want \"%T\"", err, appErr)
		}
	}
}

type FakeFetchCVDDownloader struct {
	t       *testing.T
	content string
}

func (d *FakeFetchCVDDownloader) Download(dst io.Writer, buildID string) error {
	r := strings.NewReader(d.content)
	if _, err := io.Copy(dst, r); err != nil {
		d.t.Fatal(err)
	}
	return nil
}

func TestFetchCVDHandler(t *testing.T) {
	dir := t.TempDir()
	f, err := os.Create(BuildFetchCVDFileName(dir, "1"))
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	_, err = f.Write([]byte(string("000")))
	if err != nil {
		t.Fatal(err)
	}
	downloader := &FakeFetchCVDDownloader{
		t:       t,
		content: "111",
	}

	t.Run("binary is not downloaded as it already exists", func(t *testing.T) {
		h := NewFetchCVDHandler(dir, downloader)

		err := h.Download("1")

		if err != nil {
			t.Errorf("epected <<nil>> error, got %#v", err)
		}
		content, err := ioutil.ReadFile(BuildFetchCVDFileName(dir, "1"))
		if err != nil {
			t.Fatal(err)
		}
		actual := string(content)
		expected := "000"
		if actual != expected {
			t.Errorf("expected <<%q>>, got %q", expected, actual)
		}
	})

	t.Run("binary is downloaded", func(t *testing.T) {
		h := NewFetchCVDHandler(dir, downloader)

		h.Download("2")

		content, _ := ioutil.ReadFile(BuildFetchCVDFileName(dir, "2"))
		actual := string(content)
		expected := "111"
		if actual != expected {
			t.Errorf("expected <<%q>>, got %q", expected, actual)
		}
	})
}

type AlwaysFailsFetchCVDDownloader struct{}

func (d *AlwaysFailsFetchCVDDownloader) Download(dst io.Writer, buildID string) error {
	return fmt.Errorf("downloading failed")
}

func TestFetchCVDHandlerDownloadingFails(t *testing.T) {
	dir := t.TempDir()
	h := NewFetchCVDHandler(dir, &AlwaysFailsFetchCVDDownloader{})

	err := h.Download("1")

	if err == nil {
		t.Errorf("expected an error")
	}
	if _, err := os.Stat(BuildFetchCVDFileName(dir, "1")); err == nil {
		t.Errorf("file must not have been created")
	}
}

func TestBuildFetchCVDFileName(t *testing.T) {
	actual := BuildFetchCVDFileName("/usr/bin", "1")
	expected := "/usr/bin/fetch_cvd_1"
	if actual != expected {
		t.Errorf("expected <<%q>>, got %q", expected, actual)
	}
}

func TestABFetchCVDDownloaderDownload(t *testing.T) {
	fetchCVDBinContent := "001100"
	getSignedURLRequestURI := "/android/internal/build/v3/builds/1/aosp_cf_x86_64_phone-userdebug/attempts/latest/artifacts/fetch_cvd/url?redirect=false"
	downloadRequestURI := "/android-build/builds/X/Y/Z"
	var ts *httptest.Server
	ts = httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		uri := r.URL.RequestURI()
		if uri == getSignedURLRequestURI {
			json := `{
        "signedUrl": "` + ts.URL + downloadRequestURI + `" 
 }`
			w.Header().Set("Content-Type", "application/json")
			w.Write([]byte(json))
		} else if uri == downloadRequestURI {
			w.Write([]byte(fetchCVDBinContent))
		} else {
			t.Fatalf("invalide request URI: %q\n", uri)
		}
	}))
	defer ts.Close()
	d := NewABFetchCVDDownloader(ts.Client(), ts.URL)

	var b bytes.Buffer
	d.Download(io.Writer(&b), "1")

	actual := b.String()
	if actual != fetchCVDBinContent {
		t.Errorf("expected <<%q>>, got %q", fetchCVDBinContent, actual)
	}
}

func TestABFetchCVDDownloaderDownloadWithError(t *testing.T) {
	errorMessage := "No latest build attempt for build 1"
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		errJson := `{
  "error": {
    "code": 401,
    "message": "` + errorMessage + `"
  }
}`
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(http.StatusNotFound)
		w.Write([]byte(errJson))
	}))
	defer ts.Close()
	d := NewABFetchCVDDownloader(ts.Client(), ts.URL)

	var b bytes.Buffer
	err := d.Download(io.Writer(&b), "1")

	if !strings.Contains(err.Error(), errorMessage) {
		t.Errorf("expected to contain <<%q>> in error: %#v", errorMessage, err)
	}
}
