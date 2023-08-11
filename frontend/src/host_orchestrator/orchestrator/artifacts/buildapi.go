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
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/url"
)

type BuildAPI interface {
	// Gets the latest green build ID for a given branch and target.
	GetLatestGreenBuildID(branch, target string) (string, error)

	// Downloads the specified artifact
	DownloadArtifact(name, buildID, target string, dst io.Writer) error
}

type BuildAPIError struct {
	Message string
	Code    int
}

func (e *BuildAPIError) Error() string {
	return fmt.Sprintf("%s. Code: %d", e.Message, e.Code)
}

type AndroidCIBuildAPI struct {
	BaseURL string

	client *http.Client
	creds  string
}

func NewAndroidCIBuildAPI(client *http.Client, baseURL string) *AndroidCIBuildAPI {
	return NewAndroidCIBuildAPIWithOpts(client, baseURL, AndroidCIBuildAPIOpts{})
}

type AndroidCIBuildAPIOpts struct {
	Credentials string
}

func NewAndroidCIBuildAPIWithOpts(client *http.Client, baseURL string, opts AndroidCIBuildAPIOpts) *AndroidCIBuildAPI {
	return &AndroidCIBuildAPI{
		BaseURL: baseURL,

		client: client,
		creds:  opts.Credentials,
	}
}

type listBuildResponse struct {
	Builds []build `json:"builds"`
}

type build struct {
	BuildID string `json:"buildId"`
}

func (s *AndroidCIBuildAPI) GetLatestGreenBuildID(branch, target string) (string, error) {
	const format = "%s/android/internal/build/v3/builds?" +
		"branch=%s&target=%s&buildAttemptStatus=complete&buildType=submitted&maxResults=1&successful=true"
	url := fmt.Sprintf(format, s.BaseURL, url.PathEscape(branch), url.PathEscape(target))
	res := listBuildResponse{}
	if err := s.doGETToJSON(url, &res); err != nil {
		return "", fmt.Errorf("Failed to get the latest green build id for `%s/%s`: %w", branch, target, err)
	}
	if len(res.Builds) != 1 {
		return "", fmt.Errorf("Unexpected number of build: expected 1 and got %d", len(res.Builds))
	}
	return res.Builds[0].BuildID, nil
}

func (s *AndroidCIBuildAPI) DownloadArtifact(name, buildID, target string, dst io.Writer) error {
	signedURL, err := s.getSignedURL(name, buildID, target)
	if err != nil {
		return err
	}
	return s.doGETToWriter(signedURL, dst)
}

func (s *AndroidCIBuildAPI) getSignedURL(name, buildID, target string) (string, error) {
	url := BuildDownloadArtifactSignedURL(s.BaseURL, name, buildID, target)
	res := struct {
		SignedURL string `json:"signedUrl"`
	}{}
	if err := s.doGETToJSON(url, &res); err != nil {
		return "", fmt.Errorf("Failed to get the download artifact signed url for %q (%s/%s): %w", name, buildID, target, err)
	}
	return res.SignedURL, nil
}

func (s *AndroidCIBuildAPI) doGETToWriter(url string, dst io.Writer) error {
	res, err := s.doGETCommon(url)
	if err != nil {
		return err
	}
	defer res.Body.Close()
	_, err = io.Copy(dst, res.Body)
	return err
}

func (s *AndroidCIBuildAPI) doGETToJSON(url string, body interface{}) error {
	res, err := s.doGETCommon(url)
	if err != nil {
		return err
	}
	defer res.Body.Close()
	decoder := json.NewDecoder(res.Body)
	return decoder.Decode(body)
}

func (s *AndroidCIBuildAPI) doGETCommon(url string) (*http.Response, error) {
	req, err := http.NewRequest("GET", url, nil)
	if err != nil {
		return nil, err
	}
	if s.creds != "" {
		req.Header["Authorization"] = []string{fmt.Sprintf("Bearer %s", s.creds)}
	}
	res, err := s.client.Do(req)
	if err != nil {
		return nil, err
	}
	if res.StatusCode != http.StatusOK {
		defer res.Body.Close()
		return nil, parseErrorResponse(res.Body)
	}
	return res, nil
}

func parseErrorResponse(body io.ReadCloser) error {
	decoder := json.NewDecoder(body)
	errRes := struct {
		Error struct {
			Message string
			Code    int
		}
	}{}
	if err := decoder.Decode(&errRes); err != nil {
		return err
	}
	return &BuildAPIError{Message: errRes.Error.Message, Code: errRes.Error.Code}
}

func BuildDownloadArtifactSignedURL(baseURL, name, buildID, target string) string {
	uri := fmt.Sprintf("/android/internal/build/v3/builds/%s/%s/attempts/%s/artifacts/%s/url?redirect=false",
		url.PathEscape(buildID),
		url.PathEscape(target),
		"latest",
		name)
	return baseURL + uri
}
