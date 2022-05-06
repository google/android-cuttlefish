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
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"sync"
)

type InstanceManager struct {
	fetchCVDHandler *FetchCVDHandler
}

func NewInstanceManager(fetchCVDHandler *FetchCVDHandler) *InstanceManager {
	return &InstanceManager{fetchCVDHandler}
}

func (m *InstanceManager) CreateCVD(req *CreateCVDRequest) (*Operation, error) {
	if err := validateRequest(req); err != nil {
		return nil, err
	}
	// This logic isn't complete yet, it's work in progress.
	go func() {
		err := m.fetchCVDHandler.Download(req.FetchCVDBuildID, req.BuildAPIAccessToken)
		if err != nil {
			log.Printf("error downloading fetch_cvd: %v\n", err)
		}
	}()
	return &Operation{}, nil
}

func validateRequest(r *CreateCVDRequest) error {
	if r.BuildInfo == nil ||
		r.BuildInfo.BuildID == "" ||
		r.BuildInfo.Target == "" ||
		r.FetchCVDBuildID == "" ||
		r.BuildAPIAccessToken == "" {
		return NewBadRequestError("invalid CreateCVDRequest", nil)
	}
	return nil
}

type FetchCVDHandler struct {
	dir                string
	fetchCVDDownloader FetchCVDDownloader
	mutex              sync.Mutex
}

func NewFetchCVDHandler(dir string, fetchCVDDownloader FetchCVDDownloader) *FetchCVDHandler {
	return &FetchCVDHandler{dir, fetchCVDDownloader, sync.Mutex{}}
}

func (h *FetchCVDHandler) Download(buildID, accessToken string) error {
	h.mutex.Lock()
	defer h.mutex.Unlock()
	exist, err := h.exist(buildID)
	if err != nil {
		return err
	}
	if exist {
		return nil
	}
	fileName := BuildFetchCVDFileName(h.dir, buildID)
	f, err := os.Create(fileName)
	if err != nil {
		return err
	}
	defer f.Close()
	if err := h.fetchCVDDownloader.Download(f, buildID, accessToken); err != nil {
		if removeErr := os.Remove(fileName); err != nil {
			return fmt.Errorf("%w; %v", err, removeErr)
		}
		return err
	}
	return nil
}

func (h *FetchCVDHandler) exist(buildID string) (bool, error) {
	if _, err := os.Stat(BuildFetchCVDFileName(h.dir, buildID)); err == nil {
		return true, nil
	} else if errors.Is(err, os.ErrNotExist) {
		return false, nil
	} else {
		return false, err
	}
}

func BuildFetchCVDFileName(dir, buildID string) string {
	return fmt.Sprintf("%s/fetch_cvd_%s", dir, buildID)
}

type FetchCVDDownloader interface {
	Download(dst io.Writer, buildID string, accessToken string) error
}

// Downloads fetch_cvd binary from Android Build.
type ABFetchCVDDownloader struct {
	client *http.Client
	url    string
}

func NewABFetchCVDDownloader(client *http.Client, URL string) *ABFetchCVDDownloader {
	return &ABFetchCVDDownloader{client, URL}
}

func (d *ABFetchCVDDownloader) Download(dst io.Writer, buildID string, accessToken string) error {
	url := fmt.Sprintf("%s/android/internal/build/v3/builds/%s/%s/attempts/%s/artifacts/%s?alt=media",
		d.url,
		buildID,
		"aosp_cf_x86_64_phone-userdebug",
		"latest",
		"fetch_cvd")
	req, err := http.NewRequest("GET", url, nil)
	if err != nil {
		return err
	}
	req.Header = http.Header{
		"Authorization": []string{accessToken},
		"Accept":        []string{"application/json"},
	}
	res, err := d.client.Do(req)
	if err != nil {
		return err
	}
	defer res.Body.Close()
	if res.StatusCode != http.StatusOK {
		return d.parseErrorResponse(res.Body)
	}
	_, err = io.Copy(dst, res.Body)
	if err != nil {
		return err
	}
	return nil
}

func (d *ABFetchCVDDownloader) parseErrorResponse(body io.ReadCloser) error {
	decoder := json.NewDecoder(body)
	errRes := struct {
		Error struct {
			Message string
			Code    int
		}
	}{}
	err := decoder.Decode(&errRes)
	if err != nil {
		return err
	}
	return fmt.Errorf(errRes.Error.Message)
}
