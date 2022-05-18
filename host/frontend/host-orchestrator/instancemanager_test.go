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
	"os"
	"strings"
	"testing"

	apiv1 "cuttlefish/host-orchestrator/api/v1"
)

func TestCreateCVDInvalidRequestsEmptyFields(t *testing.T) {
	im := &InstanceManager{}
	var validRequest = apiv1.CreateCVDRequest{
		BuildInfo: &apiv1.BuildInfo{
			BuildID: "1234",
			Target:  "aosp_cf_x86_64_phone-userdebug",
		},
		FetchCVDBuildID: "9999",
	}
	// Make sure the valid request is indeed valid.
	if err := validateRequest(validRequest); err != nil {
		t.Fatalf("the valid request is not valid")
	}
	var tests = []struct {
		corruptRequest func(r *apiv1.CreateCVDRequest)
	}{
		{func(r *apiv1.CreateCVDRequest) { r.BuildInfo = nil }},
		{func(r *apiv1.CreateCVDRequest) { r.BuildInfo.BuildID = "" }},
		{func(r *apiv1.CreateCVDRequest) { r.BuildInfo.Target = "" }},
		{func(r *apiv1.CreateCVDRequest) { r.FetchCVDBuildID = "" }},
	}

	for _, test := range tests {
		req := validRequest
		test.corruptRequest(&req)
		_, err := im.CreateCVD(req)
		var appErr *AppError
		if !errors.As(err, &appErr) {
			t.Errorf("error type <<\"%T\">> not found in error chain", appErr)
		}
		var emptyFieldErr EmptyFieldError
		if !errors.As(err, &emptyFieldErr) {
			t.Errorf("error type <<\"%T\">> not found in error chain", emptyFieldErr)
		}
	}
}

func TestCreateCVDNewOperationFails(t *testing.T) {
	dir := t.TempDir()
	opName := "operation-1"
	om := NewMapOM(func() string { return opName })
	fetchCVDHandler := NewFetchCVDHandler(dir, &FakeFetchCVDDownloader{t: t})
	im := NewInstanceManager(fetchCVDHandler, om)
	req := apiv1.CreateCVDRequest{
		BuildInfo: &apiv1.BuildInfo{
			BuildID: "1234",
			Target:  "aosp_cf_x86_64_phone-userdebug",
		},
		FetchCVDBuildID: "1",
	}

	im.CreateCVD(req)
	_, err := im.CreateCVD(req)

	var appErr *AppError
	if !errors.As(err, &appErr) {
		t.Errorf("error type <<\"%T\">> not found in error chain", appErr)
	}
	var newOperationErr NewOperationError
	if !errors.As(err, &newOperationErr) {
		t.Errorf("error type <<\"%T\">> not found in error chain", newOperationErr)
	}
	om.Wait(opName)
	_, ok := om.Get(opName)
	if !ok {
		t.Error("operation should exists from the first CreateCVD call")
	}
}

func TestCreateCVDFetchCVDFails(t *testing.T) {
	dir := t.TempDir()
	opName := "operation-1"
	om := NewMapOM(func() string { return opName })
	fetchCVDHandler := NewFetchCVDHandler(dir, &AlwaysFailsFetchCVDDownloader{})
	im := NewInstanceManager(fetchCVDHandler, om)
	req := apiv1.CreateCVDRequest{
		BuildInfo: &apiv1.BuildInfo{
			BuildID: "1234",
			Target:  "aosp_cf_x86_64_phone-userdebug",
		},
		FetchCVDBuildID: "1",
	}
	returnedOp := Operation{
		Name: opName,
		Done: false,
	}
	completedOp := Operation{
		Name: opName,
		Done: true,
		Result: OperationResult{
			Error: OperationResultError{"failed to download fetch_cvd"},
		},
	}

	op, _ := im.CreateCVD(req)
	if op != returnedOp {
		t.Errorf("expected <<%+v>>, got %+v", returnedOp, op)
	}
	om.Wait(opName)
	op, _ = om.Get(opName)
	if op != completedOp {
		t.Errorf("expected <<%+v>>, got %+v", completedOp, op)
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

type roundTripFunc func(r *http.Request) (*http.Response, error)

func (s roundTripFunc) RoundTrip(r *http.Request) (*http.Response, error) {
	return s(r)
}

func newMockClient(rt roundTripFunc) *http.Client {
	return &http.Client{Transport: rt}
}

func newResponseBody(content string) io.ReadCloser {
	return ioutil.NopCloser(strings.NewReader(content))
}

func TestABFetchCVDDownloaderDownload(t *testing.T) {
	fetchCVDBinContent := "001100"
	getSignedURLRequestURI := "/android/internal/build/v3/builds/1/aosp_cf_x86_64_phone-userdebug/attempts/latest/artifacts/fetch_cvd/url?redirect=false"
	downloadRequestURI := "/android-build/builds/X/Y/Z"
	url := "https://someurl.fake"
	mockClient := newMockClient(func(r *http.Request) (*http.Response, error) {
		res := &http.Response{
			StatusCode: http.StatusOK,
		}
		reqURI := r.URL.RequestURI()
		if reqURI == getSignedURLRequestURI {
			resURL := url + downloadRequestURI
			res.Body = newResponseBody(`{"signedUrl": "` + resURL + `"}`)
		} else if reqURI == downloadRequestURI {
			res.Body = newResponseBody(fetchCVDBinContent)
		} else {
			t.Fatalf("invalide request URI: %q\n", reqURI)
		}
		return res, nil
	})
	d := NewABFetchCVDDownloader(mockClient, url)

	var b bytes.Buffer
	d.Download(io.Writer(&b), "1")

	actual := b.String()
	if actual != fetchCVDBinContent {
		t.Errorf("expected <<%q>>, got %q", fetchCVDBinContent, actual)
	}
}

func TestABFetchCVDDownloaderDownloadWithError(t *testing.T) {
	errorMessage := "No latest build attempt for build 1"
	url := "https://something.fake"
	mockClient := newMockClient(func(r *http.Request) (*http.Response, error) {
		errJSON := `{
			"error": {
				"code": 401,
				"message": "` + errorMessage + `"
			}
		}`
		return &http.Response{
			StatusCode: http.StatusNotFound,
			Body:       newResponseBody(errJSON),
		}, nil
	})
	d := NewABFetchCVDDownloader(mockClient, url)

	var b bytes.Buffer
	err := d.Download(io.Writer(&b), "1")

	if !strings.Contains(err.Error(), errorMessage) {
		t.Errorf("expected to contain <<%q>> in error: %#v", errorMessage, err)
	}
}

func TestBuildGetSignedURL(t *testing.T) {
	baseURL := "http://localhost:1080"

	t.Run("regular build id", func(t *testing.T) {
		expected := "http://localhost:1080/android/internal/build/v3/builds/1/aosp_cf_x86_64_phone-userdebug/attempts/latest/artifacts/fetch_cvd/url?redirect=false"

		actual := BuildGetSignedURL(baseURL, "1")

		if actual != expected {
			t.Errorf("expected <<%q>>, got %q", expected, actual)
		}
	})

	t.Run("url-escaped build id", func(t *testing.T) {
		expected := "http://localhost:1080/android/internal/build/v3/builds/latest%3F/aosp_cf_x86_64_phone-userdebug/attempts/latest/artifacts/fetch_cvd/url?redirect=false"

		actual := BuildGetSignedURL(baseURL, "latest?")

		if actual != expected {
			t.Errorf("expected <<%q>>, got %q", expected, actual)
		}
	})
}
