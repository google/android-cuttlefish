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

package orchestrator

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

	apiv1 "cuttlefish/liboperator/api/v1"
	"cuttlefish/liboperator/operator"
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
	if err := validateRequest(&validRequest); err != nil {
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
		var appErr *operator.AppError
		if !errors.As(err, &appErr) {
			t.Errorf("error type <<\"%T\">> not found in error chain", appErr)
		}
		var emptyFieldErr EmptyFieldError
		if !errors.As(err, &emptyFieldErr) {
			t.Errorf("error type <<\"%T\">> not found in error chain", emptyFieldErr)
		}
	}
}

func TestCreateCVDFetchCVDFails(t *testing.T) {
	dir := t.TempDir()
	om := NewMapOM()
	cvdHandler := NewCVDHandler(dir, &AlwaysFailsArtifactDownloader{})
	im := NewInstanceManager(cvdHandler, om)
	req := apiv1.CreateCVDRequest{
		BuildInfo: &apiv1.BuildInfo{
			BuildID: "1234",
			Target:  "aosp_cf_x86_64_phone-userdebug",
		},
		FetchCVDBuildID: "1",
	}

	op, _ := im.CreateCVD(req)

	op, _ = om.Wait(op.Name)
	if !op.Done {
		t.Error("expected operation to be done")
	}
	if op.Result.Error.ErrorMsg != ErrMsgDownloadFetchCVDFailed {
		t.Errorf("expected <<%q>>, got %q", ErrMsgDownloadFetchCVDFailed, op.Result.Error.ErrorMsg)
	}
}

type FakeArtifactDownloader struct {
	t       *testing.T
	content string
}

func (d *FakeArtifactDownloader) Download(dst io.Writer, buildID, name string) error {
	r := strings.NewReader(d.content)
	if _, err := io.Copy(dst, r); err != nil {
		d.t.Fatal(err)
	}
	return nil
}

func TestCVDHandlerDownloadBinaryAlreadyExist(t *testing.T) {
	const fetchCVDContent = "bar"
	dir := t.TempDir()
	filename := dir + "/cvd"
	f, err := os.Create(filename)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	_, err = f.Write([]byte(fetchCVDContent))
	if err != nil {
		t.Fatal(err)
	}
	downloader := &FakeArtifactDownloader{t, "foo"}
	h := NewCVDHandler(dir, downloader)

	err = h.Download("1")

	if err != nil {
		t.Errorf("epected <<nil>> error, got %#v", err)
	}
	content, err := ioutil.ReadFile(filename)
	if err != nil {
		t.Fatal(err)
	}
	actual := string(content)
	if actual != fetchCVDContent {
		t.Errorf("expected <<%q>>, got %q", fetchCVDContent, actual)
	}
}

func TestCVDHandlerDownload(t *testing.T) {
	dir := t.TempDir()
	filename := dir + "/cvd"
	downloader := &FakeArtifactDownloader{t, "foo"}
	h := NewCVDHandler(dir, downloader)

	h.Download("1")

	content, _ := ioutil.ReadFile(filename)
	actual := string(content)
	expected := "foo"
	if actual != expected {
		t.Errorf("expected <<%q>>, got %q", expected, actual)
	}
}

func TestCVDHandlerDownload0750FileAccessIsSet(t *testing.T) {
	dir := t.TempDir()
	filename := dir + "/cvd"
	downloader := &FakeArtifactDownloader{t, "foo"}
	h := NewCVDHandler(dir, downloader)

	h.Download("1")

	stats, _ := os.Stat(filename)
	var expected os.FileMode = 0750
	if stats.Mode() != expected {
		t.Errorf("expected <<%+v>>, got %+v", expected, stats.Mode())
	}
}

func TestCVDHandlerDownloadSettingFileAccessFails(t *testing.T) {
	dir := t.TempDir()
	downloader := &FakeArtifactDownloader{t, "foo"}
	h := NewCVDHandler(dir, downloader)
	expectedErr := errors.New("error")
	h.osChmod = func(_ string, _ os.FileMode) error {
		return expectedErr
	}

	err := h.Download("1")

	if !errors.Is(err, expectedErr) {
		t.Errorf("expected <<%+v>>, got %+v", expectedErr, err)
	}
}

type AlwaysFailsArtifactDownloader struct{}

func (d *AlwaysFailsArtifactDownloader) Download(dst io.Writer, buildID, name string) error {
	return fmt.Errorf("downloading failed")
}

func TestCVDHandlerDownloadingFails(t *testing.T) {
	dir := t.TempDir()
	filename := dir + "/cvd"
	h := NewCVDHandler(dir, &AlwaysFailsArtifactDownloader{})

	err := h.Download("1")

	if err == nil {
		t.Errorf("expected an error")
	}
	if _, err := os.Stat(filename); err == nil {
		t.Errorf("file must not have been created")
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

func TestSignedURLArtifactDownloaderDownload(t *testing.T) {
	fetchCVDBinContent := "001100"
	getSignedURLRequestURI := "/android/internal/build/v3/builds/1/aosp_cf_x86_64_phone-userdebug/attempts/latest/artifacts/foo/url?redirect=false"
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
	d := NewSignedURLArtifactDownloader(mockClient, url)

	var b bytes.Buffer
	d.Download(io.Writer(&b), "1", "foo")

	actual := b.String()
	if actual != fetchCVDBinContent {
		t.Errorf("expected <<%q>>, got %q", fetchCVDBinContent, actual)
	}
}

func TestSignedURLArtifactDownloaderDownloadWithError(t *testing.T) {
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
	d := NewSignedURLArtifactDownloader(mockClient, url)

	var b bytes.Buffer
	err := d.Download(io.Writer(&b), "1", "foo")

	if !strings.Contains(err.Error(), errorMessage) {
		t.Errorf("expected to contain <<%q>> in error: %#v", errorMessage, err)
	}
}

func TestBuildGetSignedURL(t *testing.T) {
	baseURL := "http://localhost:1080"

	t.Run("regular build id", func(t *testing.T) {
		expected := "http://localhost:1080/android/internal/build/v3/builds/1/aosp_cf_x86_64_phone-userdebug/attempts/latest/artifacts/foo/url?redirect=false"

		actual := BuildGetSignedURL(baseURL, "1", "foo")

		if actual != expected {
			t.Errorf("expected <<%q>>, got %q", expected, actual)
		}
	})

	t.Run("url-escaped build id", func(t *testing.T) {
		expected := "http://localhost:1080/android/internal/build/v3/builds/latest%3F/aosp_cf_x86_64_phone-userdebug/attempts/latest/artifacts/foo/url?redirect=false"

		actual := BuildGetSignedURL(baseURL, "latest?", "foo")

		if actual != expected {
			t.Errorf("expected <<%q>>, got %q", expected, actual)
		}
	})
}
