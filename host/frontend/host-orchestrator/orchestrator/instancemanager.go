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

type AndroidBuild struct {
	ID     string
	Target string
}

type IMPaths struct {
	RootDir          string
	CVDBin           string
	ArtifactsRootDir string
	HomesRootDir     string
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
	paths                   IMPaths
	stageDownloadCVD        *StageDownloadCVD
	stageStartCVDServer     *StageStartCVDServer
	stageCreateArtifactsDir *StageCreateDirIfNotExist
	stageCreateHomesDir     *StageCreateDirIfNotExist
	stageFetchCVDMap        map[string]*StageFetchCVD
	stageFetchCVDMapMutex   sync.Mutex
}

func NewLaunchCVDProcedureBuilder(
	abURL string,
	cvdBinAB AndroidBuild,
	paths IMPaths) *LaunchCVDProcedureBuilder {
	return &LaunchCVDProcedureBuilder{
		paths: paths,
		stageDownloadCVD: &StageDownloadCVD{
			CVDBin:     paths.CVDBin,
			Build:      cvdBinAB,
			Downloader: NewCVDDownloader(NewSignedURLArtifactDownloader(http.DefaultClient, abURL)),
		},
		stageStartCVDServer: &StageStartCVDServer{
			StartCVDServerCmd: &CVDSubcmdStartCVDServer{CVDBin: paths.CVDBin},
		},
		stageCreateArtifactsDir: &StageCreateDirIfNotExist{Dir: paths.ArtifactsRootDir},
		stageCreateHomesDir:     &StageCreateDirIfNotExist{Dir: paths.HomesRootDir},
		// Fetch CVD stages will be reused to avoid downloading the artifacts of the same
		// target multiple times.
		stageFetchCVDMap: make(map[string]*StageFetchCVD),
	}
}

func (b *LaunchCVDProcedureBuilder) Build(input interface{}) Procedure {
	req, ok := input.(apiv1.CreateCVDRequest)
	if !ok {
		panic("invalid type")
	}
	return Procedure{
		b.stageDownloadCVD,
		b.stageStartCVDServer,
		b.stageCreateArtifactsDir,
		&StageCreateDirIfNotExist{
			Dir: buildArtifactsDir(b.paths.ArtifactsRootDir, *req.BuildInfo),
		},
		b.buildFetchCVDStage(req.BuildInfo),
		b.stageCreateHomesDir,
	}
}

func (b *LaunchCVDProcedureBuilder) buildFetchCVDStage(info *apiv1.BuildInfo) *StageFetchCVD {
	b.stageFetchCVDMapMutex.Lock()
	defer b.stageFetchCVDMapMutex.Unlock()
	key := fmt.Sprintf("%s_%s", info.BuildID, info.Target)
	value := b.stageFetchCVDMap[key]
	if value == nil {
		value = &StageFetchCVD{
			FetchCVDCmd: &FetchCVDCmd{
				ExecContext: exec.Command,
				Paths:       b.paths,
				BuildInfo:   *info,
			},
		}
		b.stageFetchCVDMap[key] = value
	}
	return value
}

type StageDownloadCVD struct {
	CVDBin     string
	Build      AndroidBuild
	Downloader *CVDDownloader
	mutex      sync.Mutex
}

func (s *StageDownloadCVD) Run() error {
	s.mutex.Lock()
	defer s.mutex.Unlock()
	return s.Downloader.Download(s.CVDBin, s.Build)
}

type StageStartCVDServer struct {
	StartCVDServerCmd StartCVDServerCmd
	mutex             sync.Mutex
	completed         bool
}

func (s *StageStartCVDServer) Run() error {
	s.mutex.Lock()
	defer s.mutex.Unlock()
	if s.completed {
		return nil
	}
	err := s.StartCVDServerCmd.Run(exec.Command)
	if err == nil {
		s.completed = true
	}
	return err
}

type StageCreateDirIfNotExist struct {
	Dir string
}

func (s *StageCreateDirIfNotExist) Run() error {
	// TODO(b/238431258) Use `errors.Is(err, fs.ErrExist)` instead of `os.IsExist(err)`
	// once b/236976427 is addressed.
	if err := os.Mkdir(s.Dir, 0755); err != nil && !os.IsExist(err) {
		return err
	}
	// Mkdir set the permission bits (before umask)
	return os.Chmod(s.Dir, 0755)
}

type StageFetchCVD struct {
	FetchCVDCmd *FetchCVDCmd
	mutex       sync.Mutex
	completed   bool
}

func (s *StageFetchCVD) Run() error {
	s.mutex.Lock()
	defer s.mutex.Unlock()
	if s.completed {
		return nil
	}
	stdoutStderr, err := s.FetchCVDCmd.Run()
	// NOTE: The stage is only completed when no error occurs. It's ok for this
	// stage to be retried if an error happened before.
	if err == nil {
		s.completed = true
	} else {
		log.Printf("`cvd fetch` failed with combined stdout and stderr: %q", string(stdoutStderr))
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

func (h *CVDDownloader) Download(filename string, build AndroidBuild) error {
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
	if err := h.downloader.Download(f, build, "cvd"); err != nil {
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
	Download(dst io.Writer, build AndroidBuild, name string) error
}

// Downloads the artifacts using the signed URL returned by the Android Build service.
type SignedURLArtifactDownloader struct {
	client *http.Client
	url    string
}

func NewSignedURLArtifactDownloader(client *http.Client, URL string) *SignedURLArtifactDownloader {
	return &SignedURLArtifactDownloader{client, URL}
}

func (d *SignedURLArtifactDownloader) Download(dst io.Writer, build AndroidBuild, name string) error {
	signedURL, err := d.getSignedURL(&build, name)
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

func (d *SignedURLArtifactDownloader) getSignedURL(build *AndroidBuild, name string) (string, error) {
	url := BuildGetSignedURL(d.url, *build, name)
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

func BuildGetSignedURL(baseURL string, build AndroidBuild, name string) string {
	uri := fmt.Sprintf("/android/internal/build/v3/builds/%s/%s/attempts/%s/artifacts/%s/url?redirect=false",
		url.PathEscape(build.ID),
		url.PathEscape(build.Target),
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

type FetchCVDCmd struct {
	ExecContext ExecContext
	Paths       IMPaths
	BuildInfo   apiv1.BuildInfo
}

// Runs the command and returns its combined standard output and standard error.
func (c *FetchCVDCmd) Run() ([]byte, error) {
	outDir := buildArtifactsDir(c.Paths.ArtifactsRootDir, c.BuildInfo)
	if err := os.Setenv("ANDROID_HOST_OUT", outDir); err != nil {
		return nil, err
	}
	buildFlag := fmt.Sprintf("--default_build=%s/%s", c.BuildInfo.BuildID, c.BuildInfo.Target)
	dirFlag := fmt.Sprintf("--directory=%s", outDir)
	cmd := c.ExecContext(c.Paths.CVDBin, "fetch", buildFlag, dirFlag)
	return cmd.CombinedOutput()
}

func buildArtifactsDir(baseDir string, info apiv1.BuildInfo) string {
	return fmt.Sprintf("%s/%s_%s", baseDir, info.BuildID, info.Target)
}
