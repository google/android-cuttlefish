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
	"sync/atomic"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/liboperator/api/v1"
	"github.com/google/android-cuttlefish/frontend/src/liboperator/operator"
)

type ExecContext = func(name string, arg ...string) *exec.Cmd

type InstanceManager interface {
	CreateCVD(req apiv1.CreateCVDRequest) (Operation, error)
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
	HomesRootDir     string
}

// Instance manager implementation based on execution of `cvd` tool commands.
type CVDToolInstanceManager struct {
	paths              IMPaths
	om                 OperationManager
	instanceCounter    uint32
	downloadCVDHandler *downloadCVDHandler
	fetchCVDHandler    *fetchCVDHandler
	startCVDHandler    *startCVDHandler
}

func NewCVDToolInstanceManager(
	execContext ExecContext,
	cvdBinAB AndroidBuild,
	paths IMPaths,
	cvdDwnlder CVDDownloader,
	om OperationManager) *CVDToolInstanceManager {
	return &CVDToolInstanceManager{
		paths: paths,
		om:    om,
		downloadCVDHandler: &downloadCVDHandler{
			CVDBinAB: cvdBinAB,
			CVDBin:   paths.CVDBin,
			Dwnlder:  cvdDwnlder,
		},
		fetchCVDHandler: newFetchCVDHandler(execContext, paths.CVDBin, paths.ArtifactsRootDir),
		startCVDHandler: &startCVDHandler{ExecContext: execContext, CVDBin: paths.CVDBin},
	}
}

func (m *CVDToolInstanceManager) CreateCVD(req apiv1.CreateCVDRequest) (Operation, error) {
	if err := validateRequest(&req); err != nil {
		return Operation{}, operator.NewBadRequestError("invalid CreateCVDRequest", err)
	}
	op := m.om.New()
	go m.launchCVD(req, op)
	return op, nil
}

const ErrMsgLaunchCVDFailed = "failed to launch cvd"

// TODO(b/236398043): Return more granular and informative errors.
func (m *CVDToolInstanceManager) launchCVD(req apiv1.CreateCVDRequest, op Operation) {
	var result OperationResult
	if err := m.launchCVD_(req, op); err != nil {
		log.Printf("failed to launch cvd with error: %v", err)
		result = OperationResult{
			Error: OperationResultError{ErrMsgLaunchCVDFailed},
		}
	}
	if err := m.om.Complete(op.Name, result); err != nil {
		log.Printf("failed to complete operation with error: %v", err)
	}
}

func (m *CVDToolInstanceManager) launchCVD_(req apiv1.CreateCVDRequest, op Operation) error {
	if err := m.downloadCVDHandler.Download(); err != nil {
		return err
	}
	if err := createDir(m.paths.ArtifactsRootDir, false); err != nil {
		return err
	}
	if err := createDir(m.paths.HomesRootDir, false); err != nil {
		return err
	}
	artifactsDir, err := m.fetchCVDHandler.Fetch(req.BuildInfo)
	if err != nil {
		return err
	}
	instanceNumber := atomic.AddUint32(&m.instanceCounter, 1)
	homeDir := fmt.Sprintf("%s/cvd-%d", m.paths.HomesRootDir, instanceNumber)
	if err := createDir(homeDir, true); err != nil {
		return err
	}
	if err := m.startCVDHandler.Launch(instanceNumber, artifactsDir, homeDir); err != nil {
		return err
	}
	return nil
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

func (h *fetchCVDHandler) Fetch(info *apiv1.BuildInfo) (string, error) {
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

func (h *fetchCVDHandler) getMapEntry(info *apiv1.BuildInfo) *fetchCVDMapEntry {
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
	}
	err := cvdCmd.Run()
	if err != nil {
		return fmt.Errorf("launch cvd stage failed: %w", err)
	}
	return nil
}

func createDir(dir string, failIfExist bool) error {
	// TODO(b/238431258) Use `errors.Is(err, fs.ErrExist)` instead of `os.IsExist(err)`
	// once b/236976427 is addressed.
	err := os.Mkdir(dir, 0774)
	if err != nil && ((failIfExist && os.IsExist(err)) || (!os.IsExist(err))) {
		return err
	}
	// Mkdir set the permission bits (before umask)
	return os.Chmod(dir, 0774)
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
}

func (c *cvdCommand) Run() error {
	// Makes sure cvd server daemon is running before executing the cvd command.
	if err := c.startCVDServer(); err != nil {
		return err
	}
	cmd := buildCvdCommand(c.execContext, c.androidHostOut, c.home, c.cvdBin, c.args...)
	stdoutStderr, err := cmd.CombinedOutput()
	if err != nil {
		msg := "`cvd` execution failed with combined stdout and stderr:\n" +
			"############################################\n" +
			"## BEGIN \n" +
			"############################################\n" +
			"\n%s\n\n" +
			"############################################\n" +
			"## END \n" +
			"############################################\n"
		log.Printf(msg, string(stdoutStderr))
		return fmt.Errorf("cvd execution failed: %w", err)
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
