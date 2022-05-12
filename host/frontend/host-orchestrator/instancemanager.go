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
	"net/url"
	"os"
	"sync"
)

type EmptyFieldError string

func (s EmptyFieldError) Error() string {
	return fmt.Sprintf("field %v is empty", string(s))
}

type InstanceManager struct {
	fetchCVDHandler *FetchCVDHandler
}

func NewInstanceManager(fetchCVDHandler *FetchCVDHandler) *InstanceManager {
	return &InstanceManager{fetchCVDHandler}
}

func (m *InstanceManager) CreateCVD(req *CreateCVDRequest) (*Operation, error) {
	if err := validateRequest(req); err != nil {
		return nil, NewBadRequestError("invalid CreateCVDRequest", err)
	}
	// This logic isn't complete yet, it's work in progress.
	go func() {
		err := m.fetchCVDHandler.Download(req.FetchCVDBuildID)
		if err != nil {
			log.Printf("error downloading fetch_cvd: %v\n", err)
		}
	}()
	return &Operation{}, nil
}

func validateRequest(r *CreateCVDRequest) error {
	if r.BuildInfo == nil {
		return EmptyFieldError("BuildInfo")
	}
	if r.BuildInfo.BuildID == "" {
		return EmptyFieldError("BuildInfo.BuildID")
	}
	if r.BuildInfo.Target == "" {
		return EmptyFieldError("BuildInfo.Target")
	}
	if r.FetchCVDBuildID == "" {
		return EmptyFieldError("FetchCVDBuildID")
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

func (h *FetchCVDHandler) Download(buildID string) error {
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
	if err := h.fetchCVDDownloader.Download(f, buildID); err != nil {
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
	Download(dst io.Writer, buildID string) error
}

// Downloads fetch_cvd binary from Android Build.
type ABFetchCVDDownloader struct {
	client *http.Client
	url    string
}

func NewABFetchCVDDownloader(client *http.Client, URL string) *ABFetchCVDDownloader {
	return &ABFetchCVDDownloader{client, URL}
}

func (d *ABFetchCVDDownloader) Download(dst io.Writer, buildID string) error {
	signedURL, err := d.getSignedURL(buildID)
	if err != nil {
		return err
	}
	req, err := http.NewRequest("GET", signedURL, nil)
	if err != nil {
		return err
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
	return err
}

func (d *ABFetchCVDDownloader) getSignedURL(buildID string) (string, error) {
	url := BuildGetSignedURL(d.url, buildID)
	req, err := http.NewRequest("GET", url, nil)
	if err != nil {
		return "", err
	}
	res, err := d.client.Do(req)
	if err != nil {
		return "", err
	}
	defer res.Body.Close()
	if res.StatusCode != http.StatusOK {
		return "", d.parseErrorResponse(res.Body)
	}
	decoder := json.NewDecoder(res.Body)
	body := struct {
		SignedURL string `json:"signedUrl"`
	}{}
	err = decoder.Decode(&body)
	return body.SignedURL, err
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

func BuildGetSignedURL(baseURL, buildID string) string {
	uri := fmt.Sprintf("/android/internal/build/v3/builds/%s/%s/attempts/%s/artifacts/%s/url?redirect=false",
		url.PathEscape(buildID),
		"aosp_cf_x86_64_phone-userdebug",
		"latest",
		"fetch_cvd")
	return baseURL + uri
}
