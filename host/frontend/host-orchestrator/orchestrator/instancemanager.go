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
	"os/exec"
	"sync"

	apiv1 "cuttlefish/liboperator/api/v1"
	"cuttlefish/liboperator/operator"
)

type EmptyFieldError string

func (s EmptyFieldError) Error() string {
	return fmt.Sprintf("field %v is empty", string(s))
}

type IMPaths struct {
	RootDir string
	CVDBin  string
}

type InstanceManager struct {
	OM                        OperationManager
	LaunchCVDProcedureBuilder ProcedureBuilder
}

func (m *InstanceManager) CreateCVD(req apiv1.CreateCVDRequest) (Operation, error) {
	if err := validateRequest(&req); err != nil {
		return Operation{}, operator.NewBadRequestError("invalid CreateCVDRequest", err)
	}
	op := m.OM.New()
	go m.LaunchCVD(req, op)
	return op, nil
}

const ErrMsgLaunchCVDFailed = "failed to launch cvd"

// TODO(b/236398043): Return more granular and informative errors.
func (m *InstanceManager) LaunchCVD(req apiv1.CreateCVDRequest, op Operation) {
	p := m.LaunchCVDProcedureBuilder.Build(req)
	var result OperationResult
	if err := p.Execute(); err != nil {
		log.Printf("failed to launch cvd with error: %v", err)
		result = OperationResult{
			Error: OperationResultError{ErrMsgLaunchCVDFailed},
		}
	}
	if err := m.OM.Complete(op.Name, result); err != nil {
		log.Printf("failed to complete operation with error: %v", err)
	}
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

type ProcedureStage interface {
	Run() error
}

type Procedure []ProcedureStage

func (p Procedure) Execute() error {
	for _, s := range p {
		if err := s.Run(); err != nil {
			return err
		}
	}
	return nil
}

// TODO(b/236995709): Make it generic
type ProcedureBuilder interface {
	Build(input interface{}) Procedure
}

type LaunchCVDProcedureBuilder struct {
	Paths               IMPaths
	CVDDownloader       *CVDDownloader
	StartCVDServerCmd   StartCVDServerCmd
	downloadCVDMutex    sync.Mutex
	startCVDServerMutex sync.Mutex
	cvdServerStarted    bool
}

func (b *LaunchCVDProcedureBuilder) Build(input interface{}) Procedure {
	req, ok := input.(apiv1.CreateCVDRequest)
	if !ok {
		panic("invalid type")
	}
	return Procedure{
		&StageDownloadCVD{
			CVDBin:     b.Paths.CVDBin,
			BuildID:    req.FetchCVDBuildID,
			Downloader: b.CVDDownloader,
			Mutex:      &b.downloadCVDMutex,
		},
		&StageStartCVDServer{
			StartCVDServerCmd: b.StartCVDServerCmd,
			Mutex:             &b.startCVDServerMutex,
			Started:           &b.cvdServerStarted,
		},
	}
}

type StageDownloadCVD struct {
	CVDBin     string
	BuildID    string
	Downloader *CVDDownloader
	Mutex      *sync.Mutex
}

func (s *StageDownloadCVD) Run() error {
	s.Mutex.Lock()
	defer s.Mutex.Unlock()
	return s.Downloader.Download(s.CVDBin, s.BuildID)
}

type StageStartCVDServer struct {
	StartCVDServerCmd StartCVDServerCmd
	Mutex             *sync.Mutex
	Started           *bool
}

func (s *StageStartCVDServer) Run() error {
	s.Mutex.Lock()
	defer s.Mutex.Unlock()
	if *s.Started {
		return nil
	}
	err := s.StartCVDServerCmd.Run(exec.Command)
	if err == nil {
		*s.Started = true
	}
	return err
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

const envVarAndroidHostOut = "ANDROID_HOST_OUT"

type ExecContext = func(name string, arg ...string) *exec.Cmd

type StartCVDServerCmd interface {
	Run(execContext ExecContext) error
}

type CVDSubcmdStartCVDServer struct {
	CVDBin string
}

func (c *CVDSubcmdStartCVDServer) Run(execContext ExecContext) error {
	if err := os.Setenv("ANDROID_HOST_OUT", ""); err != nil {
		return err
	}
	cmd := execContext(c.CVDBin)
	// NOTE: Stdout and Stderr should be nil so Run connects the corresponding
	// file descriptor to the null device (os.DevNull).
	// Otherwhise, `Run` will never complete. Why? a pipe will be created to handle
	// the data of the new process, this pipe will be passed over to `cvd_server`,
	// which is a daemon, hence the pipe will never reach EOF and Run will never
	// complete. Read more about it here: https://cs.opensource.google/go/go/+/refs/tags/go1.18.3:src/os/exec/exec.go;l=108-111
	cmd.Stdout = nil
	cmd.Stderr = nil
	return cmd.Run()
}
