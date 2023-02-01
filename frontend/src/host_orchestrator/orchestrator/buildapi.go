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

package orchestrator

import (
	"encoding/json"
	"fmt"
	"net/http"
	"net/url"
)

type BuildAPI interface {
	// Gets the latest green build ID for a given branch and target.
	GetLatestGreenBuildID(branch, target string) (string, error)
}

func NewBuildAPI(baseURL string) BuildAPI {
	return &buildAPIImpl{
		BaseURL: baseURL,
	}
}

type buildAPIImpl struct {
	BaseURL string
}

type listBuildResponse struct {
	Builds []build `json:"builds"`
}

type build struct {
	BuildID string `json:"buildId"`
}

func (s *buildAPIImpl) GetLatestGreenBuildID(branch, target string) (string, error) {
	const format = "%s/android/internal/build/v3/builds?" +
		"branch=%s&target=%s&buildAttemptStatus=complete&buildType=submitted&maxResults=1&successful=true"
	url := fmt.Sprintf(format, s.BaseURL, url.PathEscape(branch), url.PathEscape(target))
	res := listBuildResponse{}
	if err := doGETRequest(url, &res); err != nil {
		return "", fmt.Errorf("Failed to get the latest green build id for `%s/%s`: %w", branch, target, err)
	}
	if len(res.Builds) != 1 {
		return "", fmt.Errorf("Unexpected number of build: expected 1 and got %d", len(res.Builds))
	}
	return res.Builds[0].BuildID, nil
}

func doGETRequest(url string, body interface{}) error {
	res, err := http.Get(url)
	if err != nil {
		return err
	}
	defer res.Body.Close()
	if res.StatusCode != http.StatusOK {
		return fmt.Errorf("Request with url %q failed with status code: %q", url, res.Status)
	}
	decoder := json.NewDecoder(res.Body)
	if err := decoder.Decode(body); err != nil {
		return err
	}
	return nil
}
