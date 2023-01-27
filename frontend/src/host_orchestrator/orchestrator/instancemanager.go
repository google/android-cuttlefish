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
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"net/http"
	"net/url"
	"os"
	"os/exec"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/liboperator/api/v1"
	"github.com/google/android-cuttlefish/frontend/src/liboperator/operator"
)

type ExecContext = func(name string, arg ...string) *exec.Cmd

type Validator interface {
	Validate() error
}

type InstanceManager interface {
	CreateCVD(req apiv1.CreateCVDRequest) (apiv1.Operation, error)

	ListCVDs() (*apiv1.ListCVDsResponse, error)

	GetLogsDir(name string) string
}

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
	RuntimesRootDir  string
}

// Instance manager implementation based on execution of `cvd` tool commands.
type CVDToolInstanceManager struct {
	execContext              ExecContext
	paths                    IMPaths
	om                       OperationManager
	userArtifactsDirResolver UserArtifactsDirResolver
	instanceCounter          uint32
	downloadCVDHandler       *downloadCVDHandler
	fetchCVDHandler          *fetchCVDHandler
	startCVDHandler          *startCVDHandler
	hostValidator            Validator
}

type CVDToolInstanceManagerOpts struct {
	ExecContext              ExecContext
	CVDBinAB                 AndroidBuild
	Paths                    IMPaths
	CVDDownloader            CVDDownloader
	OperationManager         OperationManager
	UserArtifactsDirResolver UserArtifactsDirResolver
	CVDExecTimeout           time.Duration
	HostValidator            Validator
}

func NewCVDToolInstanceManager(opts *CVDToolInstanceManagerOpts) *CVDToolInstanceManager {
	return &CVDToolInstanceManager{
		execContext:              opts.ExecContext,
		paths:                    opts.Paths,
		om:                       opts.OperationManager,
		userArtifactsDirResolver: opts.UserArtifactsDirResolver,
		hostValidator:            opts.HostValidator,
		downloadCVDHandler: &downloadCVDHandler{
			CVDBinAB: opts.CVDBinAB,
			CVDBin:   opts.Paths.CVDBin,
			Dwnlder:  opts.CVDDownloader,
		},
		fetchCVDHandler: newFetchCVDHandler(opts.ExecContext, opts.Paths.CVDBin, opts.Paths.ArtifactsRootDir),
		startCVDHandler: &startCVDHandler{
			ExecContext: opts.ExecContext,
			CVDBin:      opts.Paths.CVDBin,
			Timeout:     opts.CVDExecTimeout,
		},
	}
}

func (m *CVDToolInstanceManager) CreateCVD(req apiv1.CreateCVDRequest) (apiv1.Operation, error) {
	if err := validateRequest(&req); err != nil {
		return apiv1.Operation{}, operator.NewBadRequestError("invalid CreateCVDRequest", err)
	}
	if err := m.hostValidator.Validate(); err != nil {
		return apiv1.Operation{}, err
	}
	if err := createDir(m.paths.ArtifactsRootDir); err != nil {
		return apiv1.Operation{}, err
	}
	if err := createDir(m.paths.RuntimesRootDir); err != nil {
		return apiv1.Operation{}, err
	}
	if err := m.downloadCVDHandler.Download(); err != nil {
		return apiv1.Operation{}, err
	}
	op := m.om.New()
	go m.launchCVD(req, op)
	return op, nil
}

type cvdFleetItem struct {
	InstanceName string   `json:"instance_name"`
	Status       string   `json:"status"`
	Displays     []string `json:"displays"`
}

func (m *CVDToolInstanceManager) ListCVDs() (*apiv1.ListCVDsResponse, error) {
	if err := m.downloadCVDHandler.Download(); err != nil {
		return nil, err
	}
	var stdOut string
	cvdCmd := cvdCommand{
		execContext: m.execContext,
		cvdBin:      m.paths.CVDBin,
		args:        []string{"fleet"},
		stdOut:      &stdOut,
	}
	err := cvdCmd.Run()
	if err != nil {
		return nil, err
	}
	items := make([][]cvdFleetItem, 0)
	if err := json.Unmarshal([]byte(stdOut), &items); err != nil {
		return nil, err
	}
	cvds := make([]*apiv1.CVD, 0)
	for _, s := range items {
		for _, item := range s {
			cvd := &apiv1.CVD{
				Name: item.InstanceName,
				// TODO(b/259725479): Update when `cvd fleet` prints out build information.
				BuildSource: &apiv1.BuildSource{},
				Status:      item.Status,
				Displays:    item.Displays,
			}
			cvds = append(cvds, cvd)
		}
	}
	return &apiv1.ListCVDsResponse{CVDs: cvds}, nil
}

func (m *CVDToolInstanceManager) GetLogsDir(name string) string {
	return m.paths.RuntimesRootDir + "/" + name + "/cuttlefish_runtime/logs"
}

const ErrMsgLaunchCVDFailed = "failed to launch cvd"

// TODO(b/236398043): Return more granular and informative errors.
func (m *CVDToolInstanceManager) launchCVD(req apiv1.CreateCVDRequest, op apiv1.Operation) {
	var result OperationResult
	defer func() {
		if err := m.om.Complete(op.Name, &result); err != nil {
			log.Printf("failed to complete operation with error: %v", err)
		}
	}()
	var cvd *apiv1.CVD
	var err error
	switch {
	case req.CVD.BuildSource.AndroidCIBuild != nil:
		cvd, err = m.launchFromAndroidCI(req, op)
	case req.CVD.BuildSource.UserBuild != nil:
		cvd, err = m.launchFromUserBuild(req, op)
	default:
		result = OperationResult{
			Error: operator.NewBadRequestError(
				"Invalid CreateCVDRequest, missing BuildSource information.", nil),
		}
		return
	}
	if err != nil {
		var details string
		var execError *cvdCommandExecErr
		var timeoutErr *cvdCommandTimeoutErr
		if errors.As(err, &execError) {
			details = execError.Error()
			// Overwrite err with the unwrapped error as execution errors were already logged.
			err = execError.Unwrap()
		} else if errors.As(err, &timeoutErr) {
			details = timeoutErr.Error()
		}
		result = OperationResult{Error: operator.NewInternalErrorD(ErrMsgLaunchCVDFailed, details, err)}
		log.Printf("failed to launch cvd with error: %v", err)
		return
	}
	result = OperationResult{Value: cvd}
}

func (m *CVDToolInstanceManager) launchFromAndroidCI(
	req apiv1.CreateCVDRequest, op apiv1.Operation) (*apiv1.CVD, error) {
	artifactsDir, err := m.fetchCVDHandler.Fetch(req.CVD.BuildSource.AndroidCIBuild)
	if err != nil {
		return nil, err
	}
	instanceNumber := atomic.AddUint32(&m.instanceCounter, 1)
	cvdName := fmt.Sprintf("cvd-%d", instanceNumber)
	runtimeDir := m.paths.RuntimesRootDir + "/" + cvdName
	if err := createNewDir(runtimeDir); err != nil {
		return nil, err
	}
	if err := m.startCVDHandler.Launch(instanceNumber, artifactsDir, runtimeDir); err != nil {
		return nil, err
	}
	return &apiv1.CVD{
		Name:        cvdName,
		BuildSource: req.CVD.BuildSource,
	}, nil
}

func (m *CVDToolInstanceManager) launchFromUserBuild(
	req apiv1.CreateCVDRequest, op apiv1.Operation) (*apiv1.CVD, error) {
	artifactsDir := m.userArtifactsDirResolver.GetDirPath(req.CVD.BuildSource.UserBuild.ArtifactsDir)
	if err := untarCVDHostPackage(artifactsDir); err != nil {
		return nil, err
	}
	instanceNumber := atomic.AddUint32(&m.instanceCounter, 1)
	cvdName := fmt.Sprintf("cvd-%d", instanceNumber)
	runtimeDir := m.paths.RuntimesRootDir + "/" + cvdName
	if err := createNewDir(runtimeDir); err != nil {
		return nil, err
	}
	if err := m.startCVDHandler.Launch(instanceNumber, artifactsDir, runtimeDir); err != nil {
		return nil, err
	}
	return &apiv1.CVD{
		Name:        cvdName,
		BuildSource: req.CVD.BuildSource,
	}, nil
}

const CVDHostPackageName = "cvd-host_package.tar.gz"

func untarCVDHostPackage(dir string) error {
	if err := Untar(dir, dir+"/"+CVDHostPackageName); err != nil {
		return fmt.Errorf("Failed to untar %s with error: %w", CVDHostPackageName, err)
	}
	return nil
}

func validateRequest(r *apiv1.CreateCVDRequest) error {
	if r.CVD.BuildSource == nil {
		return EmptyFieldError("BuildSource")
	}
	if r.CVD.BuildSource.AndroidCIBuild == nil && r.CVD.BuildSource.UserBuild == nil {
		return EmptyFieldError("BuildSource")
	}
	if r.CVD.BuildSource.AndroidCIBuild != nil {
		if r.CVD.BuildSource.AndroidCIBuild.BuildID == "" {
			return EmptyFieldError("BuildSource.AndroidCIBuild.BuildID")
		}
		if r.CVD.BuildSource.AndroidCIBuild.Target == "" {
			return EmptyFieldError("BuildSource.AndroidCIBuild.Target")
		}
	}
	if r.CVD.BuildSource.UserBuild != nil {
		if r.CVD.BuildSource.UserBuild.ArtifactsDir == "" {
			return EmptyFieldError("BuildSource.UserBuild.ArtifactsDir")
		}
	}
	return nil
}

type downloadCVDResult struct {
	Error error
}

type downloadCVDHandler struct {
	CVDBinAB AndroidBuild
	CVDBin   string
	Dwnlder  CVDDownloader
	mutex    sync.Mutex
	result   *downloadCVDResult
}

func (s *downloadCVDHandler) Download() error {
	s.mutex.Lock()
	defer s.mutex.Unlock()
	if s.result != nil {
		return s.result.Error
	}
	err := s.Dwnlder.Download(s.CVDBin, s.CVDBinAB)
	s.result = &downloadCVDResult{err}
	return err
}

type fetchCVDResult struct {
	OutDir string
	Error  error
}

type fetchCVDMapEntry struct {
	mutex  sync.Mutex
	result *fetchCVDResult
}

type fetchCVDHandler struct {
	execContext  ExecContext
	cvdBin       string
	artifactsDir string
	map_         map[string]*fetchCVDMapEntry
	mapMutex     sync.Mutex
}

func newFetchCVDHandler(execContext ExecContext, cvdBin, artifactsDir string) *fetchCVDHandler {
	return &fetchCVDHandler{
		execContext:  execContext,
		cvdBin:       cvdBin,
		artifactsDir: artifactsDir,
		map_:         make(map[string]*fetchCVDMapEntry),
	}
}

func (h *fetchCVDHandler) Fetch(info *apiv1.AndroidCIBuild) (string, error) {
	entry := h.getMapEntry(info)
	entry.mutex.Lock()
	defer entry.mutex.Unlock()
	if entry.result != nil {
		return entry.result.OutDir, entry.result.Error
	}
	// NOTE: The artifacts directory gets created during the execution of `cvd fetch` granting
	// owners permission to the user executing `cvd` which is relevant when extracting
	// cvd-host_package.tar.gz.
	outDir := fmt.Sprintf("%s/%s_%s", h.artifactsDir, info.BuildID, info.Target)
	buildArg := fmt.Sprintf("--default_build=%s/%s", info.BuildID, info.Target)
	dirArg := fmt.Sprintf("--directory=%s", outDir)
	cvdCmd := cvdCommand{
		execContext:    h.execContext,
		androidHostOut: "",
		home:           "",
		cvdBin:         h.cvdBin,
		args:           []string{"fetch", buildArg, dirArg},
	}
	err := cvdCmd.Run()
	if err != nil {
		entry.result = &fetchCVDResult{Error: fmt.Errorf("fetch cvd stage failed: %w", err)}
		return "", err
	}
	entry.result = &fetchCVDResult{OutDir: outDir}
	return outDir, nil
}

func (h *fetchCVDHandler) getMapEntry(info *apiv1.AndroidCIBuild) *fetchCVDMapEntry {
	h.mapMutex.Lock()
	defer h.mapMutex.Unlock()
	key := fmt.Sprintf("%s_%s", info.BuildID, info.Target)
	entry := h.map_[key]
	if entry == nil {
		entry = &fetchCVDMapEntry{}
		h.map_[key] = entry
	}
	return entry
}

const (
	daemonArg = "--daemon"
	// TODO(b/242599859): Add report_anonymous_usage_stats as a parameter to the Create CVD API.
	reportAnonymousUsageStatsArg = "--report_anonymous_usage_stats=y"
)

type startCVDHandler struct {
	ExecContext ExecContext
	CVDBin      string
	Timeout     time.Duration
}

func (h *startCVDHandler) Launch(instanceNumber uint32, artifactsDir, homeDir string) error {
	instanceNumArg := fmt.Sprintf("--base_instance_num=%d", instanceNumber)
	imgDirArg := fmt.Sprintf("--system_image_dir=%s", artifactsDir)
	cvdCmd := cvdCommand{
		execContext:    h.ExecContext,
		androidHostOut: artifactsDir,
		home:           homeDir,
		cvdBin:         h.CVDBin,
		args:           []string{"start", daemonArg, reportAnonymousUsageStatsArg, instanceNumArg, imgDirArg},
		timeout:        h.Timeout,
	}
	err := cvdCmd.Run()
	if err != nil {
		return fmt.Errorf("launch cvd stage failed: %w", err)
	}
	return nil
}

// Fails if the directory already exists.
func createNewDir(dir string) error {
	err := os.Mkdir(dir, 0774)
	if err != nil {
		return err
	}
	// Sets dir permission regardless of umask.
	return os.Chmod(dir, 0774)
}

func createDir(dir string) error {
	if err := createNewDir(dir); os.IsExist(err) {
		return nil
	} else {
		return err
	}
}

type CVDDownloader interface {
	Download(filename string, build AndroidBuild) error
}

type cvdDownloaderImpl struct {
	ad      ArtifactDownloader
	osChmod func(string, os.FileMode) error
}

func NewCVDDownloader(ad ArtifactDownloader) CVDDownloader {
	return &cvdDownloaderImpl{ad, os.Chmod}
}

func (h *cvdDownloaderImpl) Download(filename string, build AndroidBuild) error {
	exist, err := fileExist(filename)
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
	if err := h.ad.Download(f, build, "cvd"); err != nil {
		if removeErr := os.Remove(filename); err != nil {
			return fmt.Errorf("%w; %v", err, removeErr)
		}
		return err
	}
	return h.osChmod(filename, 0750)
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

func fileExist(name string) (bool, error) {
	if _, err := os.Stat(name); err == nil {
		return true, nil
	} else if os.IsNotExist(err) {
		return false, nil
	} else {
		return false, err
	}
}

const (
	cvdUser              = "_cvd-executor"
	envVarAndroidHostOut = "ANDROID_HOST_OUT"
	envVarHome           = "HOME"
)

type cvdCommand struct {
	execContext    ExecContext
	androidHostOut string
	home           string
	cvdBin         string
	args           []string
	stdOut         *string
	// if zero, there's no timeout logic.
	timeout time.Duration
}

type cvdCommandExecErr struct {
	args         []string
	stdoutStderr string
	err          error
}

func (e *cvdCommandExecErr) Error() string {
	return fmt.Sprintf("cvd execution with args %q failed with combined stdout and stderr:\n%s",
		strings.Join(e.args, " "),
		e.stdoutStderr)
}

func (e *cvdCommandExecErr) Unwrap() error { return e.err }

type cvdCommandTimeoutErr struct {
	args []string
}

func (e *cvdCommandTimeoutErr) Error() string {
	return fmt.Sprintf("cvd execution with args %q timed out", strings.Join(e.args, " "))
}

func (c *cvdCommand) Run() error {
	// Makes sure cvd server daemon is running before executing the cvd command.
	if err := c.startCVDServer(); err != nil {
		return err
	}
	done := make(chan error)
	var b bytes.Buffer
	cmd := buildCvdCommand(c.execContext, c.androidHostOut, c.home, c.cvdBin, c.args...)
	cmd.Stdout = &b
	cmd.Stderr = &b
	err := cmd.Start()
	if err != nil {
		return err
	}
	go func() { done <- cmd.Wait() }()
	var timeoutCh <-chan time.Time
	if c.timeout != 0 {
		timeoutCh = time.After(c.timeout)
	}
	select {
	case err := <-done:
		if err != nil {
			msg := "`cvd` execution failed with combined stdout and stderr:\n" +
				"############################################\n" +
				"## BEGIN \n" +
				"############################################\n" +
				"\n%s\n\n" +
				"############################################\n" +
				"## END \n" +
				"############################################\n"
			log.Printf(msg, b.String())
			return &cvdCommandExecErr{c.args, b.String(), err}
		}
		if c.stdOut != nil {
			*c.stdOut = b.String()
		}
	case <-timeoutCh:
		return &cvdCommandTimeoutErr{c.args}
	}
	return nil
}

func (c *cvdCommand) startCVDServer() error {
	cmd := buildCvdCommand(c.execContext, "", "", c.cvdBin)
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

func buildCvdCommand(execContext ExecContext,
	androidHostOut string, home string,
	cvdBin string, args ...string) *exec.Cmd {
	finalArgs := []string{"-u", cvdUser,
		envVarAndroidHostOut + "=" + androidHostOut,
		envVarHome + "=" + home,
		cvdBin,
	}
	finalArgs = append(finalArgs, args...)
	return execContext("sudo", finalArgs...)
}

// Validates whether the current host is valid to run CVDs.
type HostValidator struct {
	ExecContext ExecContext
}

func (v *HostValidator) Validate() error {
	if ok, _ := fileExist("/dev/kvm"); !ok {
		return operator.NewInternalError("Nested virtualization is not enabled.", nil)
	}
	return nil
}
