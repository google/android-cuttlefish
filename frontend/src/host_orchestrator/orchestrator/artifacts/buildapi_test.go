// Copyright 2023 Google LLC
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

package artifacts

import (
	"bytes"
	"fmt"
	"io"
	"io/ioutil"
	"net/http"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
)

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

func TestDownloadArtifact(t *testing.T) {
	binContent := "001100"
	getSignedURLRequestURI := "/android/internal/build/v3/builds/1/xyzzy/attempts/latest/artifacts/foo/url?redirect=false"
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
			res.Body = newResponseBody(binContent)
		} else {
			t.Fatalf("invalid request URI: %q\n", reqURI)
		}
		return res, nil
	})
	srv := NewAndroidCIBuildAPI(mockClient, url)

	var b bytes.Buffer
	srv.DownloadArtifact("foo", "1", "xyzzy", io.Writer(&b))

	if diff := cmp.Diff(binContent, b.String()); diff != "" {
		t.Errorf("content mismatch (-want +got):\n%s", diff)
	}
}

func TestDownloadArtifactWithErrorResponse(t *testing.T) {
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
	srv := NewAndroidCIBuildAPI(mockClient, url)

	var b bytes.Buffer
	err := srv.DownloadArtifact("foo", "1", "xyzzy", io.Writer(&b))

	if !strings.Contains(err.Error(), errorMessage) {
		t.Errorf("expected to contain <<%q>> in error: %#v", errorMessage, err)
	}
}

func TestBuildDownloadArtifactSignedURL(t *testing.T) {
	baseURL := "http://localhost:1080"

	t.Run("regular build id", func(t *testing.T) {
		expected := "http://localhost:1080/android/internal/build/v3/builds/1/xyzzy/attempts/latest/artifacts/foo/url?redirect=false"

		got := BuildDownloadArtifactSignedURL(baseURL, "foo", "1", "xyzzy")

		if diff := cmp.Diff(expected, got); diff != "" {
			t.Errorf("url mismatch (-want +got):\n%s", diff)
		}
	})

	t.Run("url-escaped android build params", func(t *testing.T) {
		expected := "http://localhost:1080/android/internal/build/v3/builds/1%3F/xyzzy%3F/attempts/latest/artifacts/foo/url?redirect=false"

		got := BuildDownloadArtifactSignedURL(baseURL, "foo", "1?", "xyzzy?")

		if diff := cmp.Diff(expected, got); diff != "" {
			t.Errorf("url mismatch (-want +got):\n%s", diff)
		}
	})
}

func TestCredentialsAddedToRequest(t *testing.T) {
	credentials := "random string"
	mockClient := newMockClient(func(r *http.Request) (*http.Response, error) {
		res := &http.Response{
			StatusCode: http.StatusOK,
		}
		if diff := cmp.Diff(r.Header["Authorization"], []string{fmt.Sprintf("Bearer %s", credentials)}); diff != "" {
			t.Errorf("Authorization header missing or malformed: %v", r.Header["Authorization"])
		}
		return res, nil
	})
	downloadRequestURI := "/android-build/builds/X/Y/Z"
	url := "https://someurl.fake"
	opts := AndroidCIBuildAPIOpts{Credentials: credentials}
	srv := NewAndroidCIBuildAPIWithOpts(mockClient, url, opts)

	_, err := srv.doGETCommon(downloadRequestURI)

	if err != nil {
		t.Errorf("GET failed: %v", err)
	}
}

func TestEmptyCredentialsIgnored(t *testing.T) {
	credentials := ""
	mockClient := newMockClient(func(r *http.Request) (*http.Response, error) {
		res := &http.Response{
			StatusCode: http.StatusOK,
		}
		if _, ok := res.Header["Authorization"]; ok {
			t.Errorf("Unexpected Authorization header in request: %v", res.Header["Authorization"])
		}
		return res, nil
	})
	downloadRequestURI := "/android-build/builds/X/Y/Z"
	url := "https://someurl.fake"
	opts := AndroidCIBuildAPIOpts{Credentials: credentials}
	srv := NewAndroidCIBuildAPIWithOpts(mockClient, url, opts)

	_, err := srv.doGETCommon(downloadRequestURI)

	if err != nil {
		t.Errorf("GET failed: %v", err)
	}
}
