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
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net/http"
	"net/url"
	"os"
	"sync"

	apiv1 "cuttlefish/liboperator/api/v1"
	"cuttlefish/liboperator/operator"
)

type EmptyFieldError string

func (s EmptyFieldError) Error() string {
	return fmt.Sprintf("field %v is empty", string(s))
}

type IMPaths struct {
	CVDBin string
}

type InstanceManager struct {
	rootDir          string
	om               OperationManager
	cvdDownloader    *CVDDownloader
	downloadCVDMutex sync.Mutex
	paths            IMPaths
}

func NewInstanceManager(rootDir string, om OperationManager, cvdDownloader *CVDDownloader) *InstanceManager {
	return &InstanceManager{
		rootDir:       rootDir,
		om:            om,
		cvdDownloader: cvdDownloader,
		paths: IMPaths{
			CVDBin: rootDir + "/cvd",
		},
	}
}

func (m *InstanceManager) CreateCVD(req apiv1.CreateCVDRequest) (Operation, error) {
	if err := validateRequest(&req); err != nil {
		return Operation{}, operator.NewBadRequestError("invalid CreateCVDRequest", err)
	}
	op := m.om.New()
	go m.LaunchCVD(req, op)
	return op, nil
}

const ErrMsgDownloadCVDFailed = "failed to download cvd"

// This logic isn't complete yet, it's work in progress.
func (m *InstanceManager) LaunchCVD(req apiv1.CreateCVDRequest, op Operation) {
	if err := m.downloadCVD(req.FetchCVDBuildID); err != nil {
		log.Printf("failed to download cvd with error: %v", err)
		result := OperationResult{
			Error: OperationResultError{ErrMsgDownloadCVDFailed},
		}
		if err := m.om.Complete(op.Name, result); err != nil {
			log.Printf("failed to complete operation with error: %v", err)
		}
		return
	}
	if err := m.om.Complete(op.Name, OperationResult{}); err != nil {
		log.Printf("failed to complete operation with error: %v", err)
	}
}

func (m *InstanceManager) downloadCVD(buildID string) error {
	m.downloadCVDMutex.Lock()
	defer m.downloadCVDMutex.Unlock()
	return m.cvdDownloader.Download(m.paths.CVDBin, buildID)
}

func validateRequest(r *apiv1.CreateCVDRequest) error {
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

type CVDDownloader struct {
	downloader ArtifactDownloader
	osChmod    func(string, os.FileMode) error
}

func NewCVDDownloader(downloader ArtifactDownloader) *CVDDownloader {
	return &CVDDownloader{downloader, os.Chmod}
}

func (h *CVDDownloader) Download(filename string, buildID string) error {
	exist, err := h.exist(filename)
	if err != nil {
		return err
	}
	if exist {
		return nil
	}
	f, err := os.Create(filename)
	if err != nil {
		return err
	}
	defer f.Close()
	if err := h.downloader.Download(f, buildID, "cvd"); err != nil {
		if removeErr := os.Remove(filename); err != nil {
			return fmt.Errorf("%w; %v", err, removeErr)
		}
		return err
	}
	return h.osChmod(filename, 0750)
}

func (h *CVDDownloader) exist(name string) (bool, error) {
	if _, err := os.Stat(name); err == nil {
		return true, nil
	} else if os.IsNotExist(err) {
		return false, nil
	} else {
		return false, err
	}
}

// Represents a downloader of artifacts hosted in Android Build (https://ci.android.com).
type ArtifactDownloader interface {
	Download(dst io.Writer, buildID, name string) error
}

// Downloads the artifacts using the signed URL returned by the Android Build service.
type SignedURLArtifactDownloader struct {
	client *http.Client
	url    string
}

func NewSignedURLArtifactDownloader(client *http.Client, URL string) *SignedURLArtifactDownloader {
	return &SignedURLArtifactDownloader{client, URL}
}

func (d *SignedURLArtifactDownloader) Download(dst io.Writer, buildID, name string) error {
	signedURL, err := d.getSignedURL(buildID, name)
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

func (d *SignedURLArtifactDownloader) getSignedURL(buildID, name string) (string, error) {
	url := BuildGetSignedURL(d.url, buildID, name)
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

func (d *SignedURLArtifactDownloader) parseErrorResponse(body io.ReadCloser) error {
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

func BuildGetSignedURL(baseURL, buildID, name string) string {
	uri := fmt.Sprintf("/android/internal/build/v3/builds/%s/%s/attempts/%s/artifacts/%s/url?redirect=false",
		url.PathEscape(buildID),
		"aosp_cf_x86_64_phone-userdebug",
		"latest",
		name)
	return baseURL + uri
}
