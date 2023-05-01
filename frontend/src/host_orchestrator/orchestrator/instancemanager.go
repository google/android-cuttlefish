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
	"log"
	"os"
	"os/exec"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/liboperator/api/v1"
	"github.com/google/android-cuttlefish/frontend/src/liboperator/operator"
	"github.com/hashicorp/go-multierror"
)

type ExecContext = func(name string, arg ...string) *exec.Cmd

type Validator interface {
	Validate() error
}

type InstanceManager interface {
	CreateCVD(req apiv1.CreateCVDRequest) (apiv1.Operation, error)

	ListCVDs() (*apiv1.ListCVDsResponse, error)

	GetLogsDir(name string) (string, error)
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

type ArtifactsFetcherFactory func(string) ArtifactsFetcher
type BuildAPIFactory func(string) BuildAPI

// Instance manager implementation based on execution of `cvd` tool commands.
type CVDToolInstanceManager struct {
	execContext              ExecContext
	paths                    IMPaths
	om                       OperationManager
	userArtifactsDirResolver UserArtifactsDirResolver
	instanceCounter          uint32
	downloadCVDHandler       *downloadCVDHandler
	artifactsMngr            *ArtifactsManager
	buildArtifactsFetcher    ArtifactsFetcherFactory
	startCVDHandler          *startCVDHandler
	hostValidator            Validator
	buildAPIFactory          BuildAPIFactory
}

type CVDToolInstanceManagerOpts struct {
	ExecContext              ExecContext
	CVDBinAB                 AndroidBuild
	Paths                    IMPaths
	OperationManager         OperationManager
	UserArtifactsDirResolver UserArtifactsDirResolver
	CVDExecTimeout           time.Duration
	HostValidator            Validator
	BuildAPIFactory          BuildAPIFactory
	UUIDGen                  func() string
}

func NewCVDToolInstanceManager(opts *CVDToolInstanceManagerOpts) *CVDToolInstanceManager {
	return &CVDToolInstanceManager{
		execContext:              opts.ExecContext,
		paths:                    opts.Paths,
		om:                       opts.OperationManager,
		userArtifactsDirResolver: opts.UserArtifactsDirResolver,
		hostValidator:            opts.HostValidator,
		downloadCVDHandler: &downloadCVDHandler{
			Build:    opts.CVDBinAB,
			Filename: opts.Paths.CVDBin,
			BuildAPI: opts.BuildAPIFactory(""), // cvd can be downloaded without credentials
		},
		artifactsMngr: NewArtifactsManager(
			opts.Paths.ArtifactsRootDir,
			opts.UUIDGen,
		),
		buildArtifactsFetcher: newArtifactsFetcherFactory(opts.ExecContext, opts.Paths.CVDBin, opts.BuildAPIFactory),
		startCVDHandler: &startCVDHandler{
			ExecContext: opts.ExecContext,
			CVDBin:      opts.Paths.CVDBin,
			Timeout:     opts.CVDExecTimeout,
		},
		buildAPIFactory: opts.BuildAPIFactory,
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

type cvdInstance struct {
	InstanceName string   `json:"instance_name"`
	Status       string   `json:"status"`
	Displays     []string `json:"displays"`
	InstanceDir  string   `json:"instance_dir"`
}

func (m *CVDToolInstanceManager) cvdFleet() ([]cvdInstance, error) {
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
	items := make([][]cvdInstance, 0)
	if err := json.Unmarshal([]byte(stdOut), &items); err != nil {
		return nil, err
	}
	if len(items) == 0 {
		return []cvdInstance{}, nil
	}
	// Host orchestrator only works with one instances group.
	return items[0], nil
}

func (m *CVDToolInstanceManager) ListCVDs() (*apiv1.ListCVDsResponse, error) {
	fleetItems, err := m.cvdFleet()
	if err != nil {
		return nil, err
	}
	cvds := make([]*apiv1.CVD, 0)
	for _, item := range fleetItems {
		cvd := &apiv1.CVD{
			Name: item.InstanceName,
			// TODO(b/259725479): Update when `cvd fleet` prints out build information.
			BuildSource: &apiv1.BuildSource{},
			Status:      item.Status,
			Displays:    item.Displays,
		}
		cvds = append(cvds, cvd)
	}
	return &apiv1.ListCVDsResponse{CVDs: cvds}, nil
}

func (m *CVDToolInstanceManager) GetLogsDir(name string) (string, error) {
	instances, err := m.cvdFleet()
	if err != nil {
		return "", err
	}
	ok, ins := cvdInstances(instances).findByName(name)
	if !ok {
		return "", operator.NewNotFoundError(fmt.Sprintf("Instance %q not found", name), nil)
	}
	return ins.InstanceDir + "/logs", nil
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
	case req.CVD.BuildSource.AndroidCIBuildSource != nil:
		cvd, err = m.launchFromAndroidCI(req, op)
	case req.CVD.BuildSource.UserBuildSource != nil:
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

const (
	// TODO(b/267525748): Make these values configurable.
	mainBuildDefaultBranch = "aosp-master"
	mainBuildDefaultTarget = "aosp_cf_x86_64_phone-userdebug"
)

func defaultMainBuild() *apiv1.AndroidCIBuild {
	return &apiv1.AndroidCIBuild{Branch: mainBuildDefaultBranch, Target: mainBuildDefaultTarget}
}

func (m *CVDToolInstanceManager) launchFromAndroidCI(
	req apiv1.CreateCVDRequest, op apiv1.Operation) (*apiv1.CVD, error) {
	var mainBuild *apiv1.AndroidCIBuild = defaultMainBuild()
	var kernelBuild *apiv1.AndroidCIBuild
	var bootloaderBuild *apiv1.AndroidCIBuild
	var systemImgBuild *apiv1.AndroidCIBuild
	var credentials string
	if req.CVD.BuildSource != nil && req.CVD.BuildSource.AndroidCIBuildSource != nil {
		buildSource := req.CVD.BuildSource.AndroidCIBuildSource
		if buildSource.MainBuild != nil {
			*mainBuild = *req.CVD.BuildSource.AndroidCIBuildSource.MainBuild
		}
		if buildSource.KernelBuild != nil {
			kernelBuild = &apiv1.AndroidCIBuild{}
			*kernelBuild = *buildSource.KernelBuild
		}
		if buildSource.BootloaderBuild != nil {
			bootloaderBuild = &apiv1.AndroidCIBuild{}
			*bootloaderBuild = *buildSource.BootloaderBuild
		}
		if buildSource.SystemImageBuild != nil {
			systemImgBuild = &apiv1.AndroidCIBuild{}
			*systemImgBuild = *buildSource.SystemImageBuild
		}
		credentials = buildSource.Credentials
	}
	buildAPI := m.buildAPIFactory(credentials)
	if err := updateBuildsWithLatestGreenBuildID(buildAPI,
		[]*apiv1.AndroidCIBuild{mainBuild, kernelBuild, bootloaderBuild, systemImgBuild}); err != nil {
		return nil, err
	}
	var mainBuildDir, kernelBuildDir, bootloaderBuildDir string
	var mainBuildErr, kernelBuildErr, bootloaderBuildErr error
	var wg sync.WaitGroup
	wg.Add(1)
	fetcher := m.buildArtifactsFetcher(credentials)
	go func() {
		defer wg.Done()
		var extraCVDOptions *ExtraCVDOptions = nil
		if systemImgBuild != nil {
			extraCVDOptions = &ExtraCVDOptions{
				SystemImgBuildID: systemImgBuild.BuildID,
				SystemImgTarget:  systemImgBuild.Target,
			}
		}
		mainBuildDir, mainBuildErr = m.artifactsMngr.GetCVDBundle(
			mainBuild.BuildID, mainBuild.Target, extraCVDOptions, fetcher)
	}()
	if kernelBuild != nil {
		wg.Add(1)
		go func() {
			defer wg.Done()
			kernelBuildDir, kernelBuildErr = m.artifactsMngr.GetKernelBundle(
				kernelBuild.BuildID, kernelBuild.Target, fetcher)
		}()
	}
	if bootloaderBuild != nil {
		wg.Add(1)
		go func() {
			defer wg.Done()
			bootloaderBuildDir, bootloaderBuildErr = m.artifactsMngr.GetBootloaderBundle(
				bootloaderBuild.BuildID, bootloaderBuild.Target, fetcher)
		}()
	}
	wg.Wait()
	var merr error
	for _, err := range []error{mainBuildErr, kernelBuildErr, bootloaderBuildErr} {
		if err != nil {
			merr = multierror.Append(merr, err)
		}
	}
	if merr != nil {
		return nil, merr
	}
	instanceNumber := atomic.AddUint32(&m.instanceCounter, 1)
	cvdName := fmt.Sprintf("cvd-%d", instanceNumber)
	runtimeDir := m.paths.RuntimesRootDir + "/" + cvdName
	if err := createNewDir(runtimeDir); err != nil {
		return nil, err
	}
	startParams := startCVDParams{
		InstanceNumber:   instanceNumber,
		MainArtifactsDir: mainBuildDir,
		RuntimeDir:       runtimeDir,
		KernelDir:        kernelBuildDir,
		BootloaderDir:    bootloaderBuildDir,
	}
	if err := m.startCVDHandler.Start(startParams); err != nil {
		return nil, err
	}
	cvd := &apiv1.CVD{}
	*cvd = *req.CVD
	cvd.Name = cvdName
	return cvd, nil
}

func (m *CVDToolInstanceManager) launchFromUserBuild(
	req apiv1.CreateCVDRequest, op apiv1.Operation) (*apiv1.CVD, error) {
	artifactsDir := m.userArtifactsDirResolver.GetDirPath(req.CVD.BuildSource.UserBuildSource.ArtifactsDir)
	if err := untarCVDHostPackage(artifactsDir); err != nil {
		return nil, err
	}
	instanceNumber := atomic.AddUint32(&m.instanceCounter, 1)
	cvdName := fmt.Sprintf("cvd-%d", instanceNumber)
	runtimeDir := m.paths.RuntimesRootDir + "/" + cvdName
	if err := createNewDir(runtimeDir); err != nil {
		return nil, err
	}
	startParams := startCVDParams{
		InstanceNumber:   instanceNumber,
		MainArtifactsDir: artifactsDir,
		RuntimeDir:       runtimeDir,
	}
	if err := m.startCVDHandler.Start(startParams); err != nil {
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
	if r.CVD.BuildSource.AndroidCIBuildSource == nil && r.CVD.BuildSource.UserBuildSource == nil {
		return EmptyFieldError("BuildSource")
	}
	if r.CVD.BuildSource.UserBuildSource != nil {
		if r.CVD.BuildSource.UserBuildSource.ArtifactsDir == "" {
			return EmptyFieldError("BuildSource.UserBuild.ArtifactsDir")
		}
	}
	return nil
}

// Makes sure `cvd` gets downloaded once.
type downloadCVDHandler struct {
	Build    AndroidBuild
	Filename string
	BuildAPI BuildAPI

	mutex sync.Mutex
}

func (h *downloadCVDHandler) Download() error {
	h.mutex.Lock()
	defer h.mutex.Unlock()
	if err := h.download(); err != nil {
		return fmt.Errorf("failed downloading cvd file: %w", err)
	}
	return os.Chmod(h.Filename, 0750)
}

func (h *downloadCVDHandler) download() error {
	exist, err := fileExist(h.Filename)
	if err != nil {
		return fmt.Errorf("failed to test if the `cvd` file %q does exist: %w", h.Filename, err)
	}
	if exist {
		return nil
	}
	f, err := os.Create(h.Filename)
	if err != nil {
		return fmt.Errorf("failed to create the `cvd` file %q: %w", h.Filename, err)
	}
	var downloadErr error
	defer func() {
		if err := f.Close(); err != nil {
			log.Printf("failed closing `cvd` file %q file, error: %v", h.Filename, err)
		}
		if downloadErr != nil {
			if err := os.Remove(h.Filename); err != nil {
				log.Printf("failed removing  `cvd` file %q: %v", h.Filename, err)
			}
		}

	}()
	downloadErr = h.BuildAPI.DownloadArtifact("cvd", h.Build.ID, h.Build.Target, f)
	return downloadErr
}

// Fetches artifacts using the build api or the fetch_cvd tool as necessary.
type combinedArtifactFetcher struct {
	execContext ExecContext
	cvdBin      string
	buildAPI    BuildAPI
	credentials string
}

func newArtifactsFetcherFactory(execContext ExecContext, cvdBin string, buildAPIFactory BuildAPIFactory) ArtifactsFetcherFactory {
	return func(credentials string) ArtifactsFetcher {
		return &combinedArtifactFetcher{
			execContext: execContext,
			cvdBin:      cvdBin,
			buildAPI:    buildAPIFactory(credentials),
			credentials: credentials,
		}
	}
}

// The artifacts directory gets created during the execution of `cvd fetch` granting owners permission to the
// user executing `cvd` which is relevant when extracting cvd-host_package.tar.gz.
func (f *combinedArtifactFetcher) FetchCVD(outDir, buildID, target string, extraOptions *ExtraCVDOptions) error {
	cmdArgs := []string{
		"fetch",
		fmt.Sprintf("--directory=%s", outDir),
		fmt.Sprintf("--default_build=%s/%s", buildID, target),
	}
	if extraOptions != nil {
		cmdArgs = append(cmdArgs,
			fmt.Sprintf("--system_build=%s/%s", extraOptions.SystemImgBuildID, extraOptions.SystemImgTarget))
	}
	if f.credentials != "" {
		file, err := createCredentialsFile(f.credentials)
		if err != nil {
			return err
		}
		// DO NOT ENCAPSULATE THIS BLOCK: The file must not be closed until after the cvd command has run.
		defer file.Close()
		cmdArgs = append(cmdArgs, fmt.Sprintf("--credential_source=/proc/self/fd/%d", int(file.Fd())))
	}
	cvdCmd := cvdCommand{
		execContext:    f.execContext,
		androidHostOut: "",
		home:           "",
		cvdBin:         f.cvdBin,
		args:           cmdArgs,
	}
	return cvdCmd.Run()
}

func (f *combinedArtifactFetcher) FetchArtifacts(outDir, buildID, target string, artifactNames ...string) error {
	var chans []chan error
	for _, name := range artifactNames {
		ch := make(chan error)
		chans = append(chans, ch)
		go func(name string) {
			defer close(ch)
			filename := outDir + "/" + name
			if err := downloadArtifactToFile(f.buildAPI, filename, name, buildID, target); err != nil {
				ch <- err
			}
		}(name)
	}
	var merr error
	for _, ch := range chans {
		for err := range ch {
			merr = multierror.Append(merr, err)
		}
	}
	return merr
}

func createCredentialsFile(content string) (*os.File, error) {
	p1, p2, err := os.Pipe()
	if err != nil {
		return nil, fmt.Errorf("Failed to create pipe for credentials: %w", err)
	}
	go func(f *os.File) {
		defer f.Close()
		_, err = f.Write([]byte(content))
		log.Printf("Failed to write credentials to file: %v\n", err)
		// Can't return this error without risking a deadlock when the pipe buffer fills up.
	}(p2)
	return p1, nil
}

const (
	daemonArg = "--daemon"
	// TODO(b/242599859): Add report_anonymous_usage_stats as a parameter to the Create CVD API.
	reportAnonymousUsageStatsArg = "--report_anonymous_usage_stats=y"
	groupNameArg                 = "--group_name=cvd"
)

type startCVDHandler struct {
	ExecContext ExecContext
	CVDBin      string
	Timeout     time.Duration
}

type startCVDParams struct {
	InstanceNumber   uint32
	MainArtifactsDir string
	RuntimeDir       string
	// OPTIONAL. If set, kernel relevant artifacts will be pulled from this dir.
	KernelDir string
	// OPTIONAL. If set, bootloader relevant artifacts will be pulled from this dir.
	BootloaderDir string
}

func (h *startCVDHandler) Start(p startCVDParams) error {
	instanceNumArg := fmt.Sprintf("--base_instance_num=%d", p.InstanceNumber)
	imgDirArg := fmt.Sprintf("--system_image_dir=%s", p.MainArtifactsDir)
	cvdCmd := cvdCommand{
		execContext:    h.ExecContext,
		androidHostOut: p.MainArtifactsDir,
		home:           p.RuntimeDir,
		cvdBin:         h.CVDBin,
		args: []string{groupNameArg, "start",
			daemonArg, reportAnonymousUsageStatsArg, instanceNumArg, imgDirArg},
		timeout: h.Timeout,
	}
	if p.KernelDir != "" {
		cvdCmd.args = append(cvdCmd.args, fmt.Sprintf("--kernel_path=%s/bzImage", p.KernelDir))
	}
	if p.BootloaderDir != "" {
		cvdCmd.args = append(cvdCmd.args, fmt.Sprintf("--bootloader=%s/u-boot.rom", p.BootloaderDir))
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

func buildCvdCommand(execContext ExecContext, androidHostOut string, home string, cvdBin string, args ...string) *exec.Cmd {
	newArgs := []string{"-u", cvdUser, envVarHome + "=" + home}
	if androidHostOut != "" {
		newArgs = append(newArgs, envVarAndroidHostOut+"="+androidHostOut)
	}
	newArgs = append(newArgs, cvdBin)
	newArgs = append(newArgs, args...)
	return execContext("sudo", newArgs...)
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

// Helper to update the passed builds with latest green BuildID if build is not nil and BuildId is empty.
func updateBuildsWithLatestGreenBuildID(buildAPI BuildAPI, builds []*apiv1.AndroidCIBuild) error {
	var chans []chan error
	for _, build := range builds {
		ch := make(chan error)
		chans = append(chans, ch)
		go func(build *apiv1.AndroidCIBuild) {
			defer close(ch)
			if build != nil && build.BuildID == "" {
				if err := updateBuildWithLatestGreenBuildID(buildAPI, build); err != nil {
					ch <- err
				}
			}
		}(build)
	}
	var merr error
	for _, ch := range chans {
		for err := range ch {
			merr = multierror.Append(merr, err)
		}
	}
	return merr
}

// Helper to update the passed `build` with latest green BuildID.
func updateBuildWithLatestGreenBuildID(buildAPI BuildAPI, build *apiv1.AndroidCIBuild) error {
	buildID, err := buildAPI.GetLatestGreenBuildID(build.Branch, build.Target)
	if err != nil {
		return err
	}
	build.BuildID = buildID
	return nil
}

// Download artifacts helper. Fails if file already exists.
func downloadArtifactToFile(buildAPI BuildAPI, filename, artifactName, buildID, target string) error {
	exist, err := fileExist(target)
	if err != nil {
		return fmt.Errorf("download artifact %q failed: %w", filename, err)
	}
	if exist {
		return fmt.Errorf("download artifact %q failed: file already exists", filename)
	}
	f, err := os.Create(filename)
	if err != nil {
		return fmt.Errorf("download artifact %q failed: %w", filename, err)
	}
	var downloadErr error
	defer func() {
		if err := f.Close(); err != nil {
			log.Printf("download artifact: failed closing %q, error: %v", filename, err)
		}
		if downloadErr != nil {
			if err := os.Remove(filename); err != nil {
				log.Printf("download artifact: failed removing %q: %v", filename, err)
			}
		}
	}()
	downloadErr = buildAPI.DownloadArtifact(artifactName, buildID, target, f)
	return downloadErr
}

type cvdInstances []cvdInstance

func (s cvdInstances) findByName(name string) (bool, cvdInstance) {
	for _, e := range s {
		if e.InstanceName == name {
			return true, e
		}
	}
	return false, cvdInstance{}
}
