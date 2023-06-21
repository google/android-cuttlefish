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
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"syscall"
	"time"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/liboperator/api/v1"
	"github.com/google/android-cuttlefish/frontend/src/liboperator/operator"
	"github.com/hashicorp/go-multierror"
)

type ExecContext = func(ctx context.Context, name string, arg ...string) *exec.Cmd

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
	CVDToolsDir      string
	ArtifactsRootDir string
	RuntimesRootDir  string
}

func (p *IMPaths) CVDBin() string {
	return filepath.Join(p.CVDToolsDir, "cvd")
}

func (p *IMPaths) FetchCVDBin() string {
	return filepath.Join(p.CVDToolsDir, "fetch_cvd")
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
	CVDToolsVersion          AndroidBuild
	Paths                    IMPaths
	OperationManager         OperationManager
	UserArtifactsDirResolver UserArtifactsDirResolver
	CVDStartTimeout          time.Duration
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
		downloadCVDHandler: newDownloadCVDHandler(
			opts.CVDToolsVersion,
			&opts.Paths,
			opts.BuildAPIFactory(""), // cvd can be downloaded without credentials
		),
		artifactsMngr: NewArtifactsManager(
			opts.Paths.ArtifactsRootDir,
			opts.UUIDGen,
		),
		buildArtifactsFetcher: newArtifactsFetcherFactory(opts.ExecContext, opts.Paths.FetchCVDBin(), opts.BuildAPIFactory),
		startCVDHandler: &startCVDHandler{
			ExecContext: opts.ExecContext,
			CVDBin:      opts.Paths.CVDBin(),
			Timeout:     opts.CVDStartTimeout,
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
	if err := createRuntimesRootDir(m.paths.RuntimesRootDir); err != nil {
		return apiv1.Operation{}, fmt.Errorf("failed creating cuttlefish runtime directory: %w", err)
	}
	if err := m.downloadCVDHandler.Download(); err != nil {
		return apiv1.Operation{}, err
	}
	op := m.om.New()
	go m.launchCVD(req, op)
	return op, nil
}

// Makes runtime artifacts owned by `cvdnetwork` group.
func createRuntimesRootDir(name string) error {
	if err := createDir(name); err != nil {
		return err
	}
	return os.Chmod(name, 0774|os.ModeSetgid)
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
	stdout := &bytes.Buffer{}
	cvdCmd := newCVDCommand(m.execContext, m.paths.CVDBin(), []string{"fleet"}, cvdCommandOpts{Stdout: stdout})
	err := cvdCmd.Run()
	if err != nil {
		return nil, err
	}
	items := make([][]cvdInstance, 0)
	if err := json.Unmarshal(stdout.Bytes(), &items); err != nil {
		log.Printf("Failed parsing `cvd fleet` ouput. Output: \n\n%s\n", cmdOutputLogMessage(stdout.String()))
		return nil, fmt.Errorf("failed parsing `cvd fleet` output: %w", err)
	}
	if len(items) == 0 {
		return []cvdInstance{}, nil
	}
	// Host orchestrator only works with one instances group.
	return items[0], nil
}

func fleetToCVDs(val []cvdInstance) []*apiv1.CVD {
	result := make([]*apiv1.CVD, len(val))
	for i, item := range val {
		result[i] = &apiv1.CVD{
			Name: item.InstanceName,
			// TODO(b/259725479): Update when `cvd fleet` prints out build information.
			BuildSource: &apiv1.BuildSource{},
			Status:      item.Status,
			Displays:    item.Displays,
		}
	}
	return result
}

func (m *CVDToolInstanceManager) ListCVDs() (*apiv1.ListCVDsResponse, error) {
	fleet, err := m.cvdFleet()
	if err != nil {
		return nil, err
	}
	return &apiv1.ListCVDsResponse{CVDs: fleetToCVDs(fleet)}, nil
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

func (m *CVDToolInstanceManager) launchCVD(req apiv1.CreateCVDRequest, op apiv1.Operation) {
	result := m.launchCVDResult(req, op)
	if err := m.om.Complete(op.Name, result); err != nil {
		log.Printf("error completing launch cvd operation %q: %v\n", op.Name, err)
	}
}

func (m *CVDToolInstanceManager) launchCVDResult(req apiv1.CreateCVDRequest, op apiv1.Operation) *OperationResult {
	instancesCount := 1 + req.AdditionalInstancesNum
	var instanceNumbers []uint32
	var err error
	switch {
	case req.CVD.BuildSource.AndroidCIBuildSource != nil:
		instanceNumbers, err = m.launchFromAndroidCI(req.CVD.BuildSource.AndroidCIBuildSource, instancesCount, op)
	case req.CVD.BuildSource.UserBuildSource != nil:
		instanceNumbers, err = m.launchFromUserBuild(req.CVD.BuildSource.UserBuildSource, instancesCount, op)
	default:
		return &OperationResult{
			Error: operator.NewBadRequestError(
				"Invalid CreateCVDRequest, missing BuildSource information.", nil),
		}
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
		log.Printf("failed to launch cvd with error: %v", err)
		return &OperationResult{Error: operator.NewInternalErrorD(ErrMsgLaunchCVDFailed, details, err)}
	}
	fleet, err := m.cvdFleet()
	if err != nil {
		return &OperationResult{Error: operator.NewInternalError(ErrMsgLaunchCVDFailed, err)}
	}
	relevant := []cvdInstance{}
	for _, item := range fleet {
		n, err := nameToNumber(item.InstanceName)
		if err != nil {
			return &OperationResult{Error: operator.NewInternalError(ErrMsgLaunchCVDFailed, err)}
		}
		if contains(instanceNumbers, uint32(n)) {
			relevant = append(relevant, item)
		}
	}
	res := &apiv1.CreateCVDResponse{
		CVDs: fleetToCVDs(relevant),
	}
	return &OperationResult{Value: res}
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
	buildSource *apiv1.AndroidCIBuildSource, instancesCount uint32, op apiv1.Operation) ([]uint32, error) {
	var mainBuild *apiv1.AndroidCIBuild = defaultMainBuild()
	var kernelBuild *apiv1.AndroidCIBuild
	var bootloaderBuild *apiv1.AndroidCIBuild
	var systemImgBuild *apiv1.AndroidCIBuild
	if buildSource.MainBuild != nil {
		*mainBuild = *buildSource.MainBuild
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
	credentials := buildSource.Credentials
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
	startParams := startCVDParams{
		InstanceNumbers:  m.newInstanceNumbers(instancesCount),
		MainArtifactsDir: mainBuildDir,
		RuntimeDir:       m.paths.RuntimesRootDir,
		KernelDir:        kernelBuildDir,
		BootloaderDir:    bootloaderBuildDir,
	}
	if err := m.startCVDHandler.Start(startParams); err != nil {
		return nil, err
	}
	// TODO: Remove once `acloud CLI` gets deprecated.
	if contains(startParams.InstanceNumbers, 1) {
		go runAcloudSetup(m.execContext, m.paths.ArtifactsRootDir, mainBuildDir, m.paths.RuntimesRootDir)
	}
	return startParams.InstanceNumbers, nil
}

func (m *CVDToolInstanceManager) launchFromUserBuild(
	buildSource *apiv1.UserBuildSource, instancesCount uint32, op apiv1.Operation) ([]uint32, error) {
	artifactsDir := m.userArtifactsDirResolver.GetDirPath(buildSource.ArtifactsDir)
	if err := untarCVDHostPackage(artifactsDir); err != nil {
		return nil, err
	}
	startParams := startCVDParams{
		InstanceNumbers:  m.newInstanceNumbers(instancesCount),
		MainArtifactsDir: artifactsDir,
		RuntimeDir:       m.paths.RuntimesRootDir,
	}
	if err := m.startCVDHandler.Start(startParams); err != nil {
		return nil, err
	}
	// TODO: Remove once `acloud CLI` gets deprecated.
	if contains(startParams.InstanceNumbers, 1) {
		go runAcloudSetup(m.execContext, m.paths.ArtifactsRootDir, artifactsDir, m.paths.RuntimesRootDir)
	}
	return startParams.InstanceNumbers, nil
}

func (m *CVDToolInstanceManager) newInstanceNumbers(n uint32) []uint32 {
	result := []uint32{}
	for i := 0; i < int(n); i++ {
		num := atomic.AddUint32(&m.instanceCounter, 1)
		result = append(result, num)
	}
	return result
}

const CVDHostPackageName = "cvd-host_package.tar.gz"

func untarCVDHostPackage(dir string) error {
	if err := Untar(dir, dir+"/"+CVDHostPackageName); err != nil {
		return fmt.Errorf("Failed to untar %s with error: %w", CVDHostPackageName, err)
	}
	return nil
}

func validateRequest(r *apiv1.CreateCVDRequest) error {
	if r.CVD == nil {
		return EmptyFieldError("CVD")
	}
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
	Build            AndroidBuild
	cvdFilename      string
	fetchCVDFilename string
	BuildAPI         BuildAPI

	mutex sync.Mutex
}

func newDownloadCVDHandler(build AndroidBuild, paths *IMPaths, buildAPI BuildAPI) *downloadCVDHandler {
	return &downloadCVDHandler{
		Build:            build,
		cvdFilename:      paths.CVDBin(),
		fetchCVDFilename: paths.FetchCVDBin(),
		BuildAPI:         buildAPI,
	}
}

func (h *downloadCVDHandler) Download() error {
	h.mutex.Lock()
	defer h.mutex.Unlock()
	if err := h.download(h.cvdFilename); err != nil {
		return fmt.Errorf("failed downloading cvd file: %w", err)
	}
	if err := h.download(h.fetchCVDFilename); err != nil {
		return fmt.Errorf("failed downloading fetch_cvd file: %w", err)
	}
	return nil
}

func (h *downloadCVDHandler) download(filename string) error {
	exist, err := fileExist(filename)
	if err != nil {
		return fmt.Errorf("failed to test if the `%s` file %q does exist: %w", filepath.Base(filename), filename, err)
	}
	if exist {
		return nil
	}
	f, err := os.Create(filename)
	if err != nil {
		return fmt.Errorf("failed to create the `%s` file %q: %w", filepath.Base(filename), filename, err)
	}
	var downloadErr error
	defer func() {
		if err := f.Close(); err != nil {
			log.Printf("failed closing `%s` file %q file, error: %v", filepath.Base(filename), filename, err)
		}
		if downloadErr != nil {
			if err := os.Remove(filename); err != nil {
				log.Printf("failed removing  `%s` file %q: %v", filepath.Base(filename), filename, err)
			}
		}

	}()
	if err := h.BuildAPI.DownloadArtifact(filepath.Base(filename), h.Build.ID, h.Build.Target, f); err != nil {
		return err
	}
	return os.Chmod(filename, 0750)
}

// Fetches artifacts using the build api or the fetch_cvd tool as necessary.
type combinedArtifactFetcher struct {
	execContext ExecContext
	fetchCVDBin string
	buildAPI    BuildAPI
	credentials string
}

func newArtifactsFetcherFactory(execContext ExecContext, fetchCVDBin string, buildAPIFactory BuildAPIFactory) ArtifactsFetcherFactory {
	return func(credentials string) ArtifactsFetcher {
		return &combinedArtifactFetcher{
			execContext: execContext,
			fetchCVDBin: fetchCVDBin,
			buildAPI:    buildAPIFactory(credentials),
			credentials: credentials,
		}
	}
}

// The artifacts directory gets created during the execution of `fetch_cvd` granting access to the cvdnetwork group
// which translated to granting the necessary permissions to the cvd executor user.
func (f *combinedArtifactFetcher) FetchCVD(outDir, buildID, target string, extraOptions *ExtraCVDOptions) error {
	args := []string{
		fmt.Sprintf("--directory=%s", outDir),
		fmt.Sprintf("--default_build=%s/%s", buildID, target),
	}
	if extraOptions != nil {
		args = append(args,
			fmt.Sprintf("--system_build=%s/%s", extraOptions.SystemImgBuildID, extraOptions.SystemImgTarget))
	}
	var file *os.File
	var err error
	fetchCmd := f.execContext(context.TODO(), f.fetchCVDBin, args...)
	if f.credentials != "" {
		if file, err = createCredentialsFile(f.credentials); err != nil {
			return err
		}
		defer file.Close()
		// This is necessary for the subprocess to inherit the file.
		fetchCmd.ExtraFiles = append(fetchCmd.ExtraFiles, file)
		// The actual fd number is not retained, the lowest available number is used instead.
		fd := 3 + len(fetchCmd.ExtraFiles) - 1
		fetchCmd.Args = append(fetchCmd.Args, fmt.Sprintf("--credential_source=/proc/self/fd/%d", fd))
	}
	out, err := fetchCmd.CombinedOutput()
	if err != nil {
		logCombinedStdoutStderr(fetchCmd, string(out))
		return err
	}
	// TODO(b/286466643): Remove this hack once cuttlefish is capable of booting from read-only artifacts again.
	chmodCmd := f.execContext(context.TODO(), "chmod", "-R", "g+rw", outDir)
	chmodOut, err := chmodCmd.CombinedOutput()
	if err != nil {
		logCombinedStdoutStderr(chmodCmd, string(chmodOut))
		return err
	}
	return nil
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
		if _, err := f.Write([]byte(content)); err != nil {
			log.Printf("Failed to write credentials to file: %v\n", err)
			// Can't return this error without risking a deadlock when the pipe buffer fills up.
		}
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
	InstanceNumbers  []uint32
	MainArtifactsDir string
	RuntimeDir       string
	// OPTIONAL. If set, kernel relevant artifacts will be pulled from this dir.
	KernelDir string
	// OPTIONAL. If set, bootloader relevant artifacts will be pulled from this dir.
	BootloaderDir string
}

func (h *startCVDHandler) Start(p startCVDParams) error {
	args := []string{groupNameArg, "start", daemonArg, reportAnonymousUsageStatsArg}
	if len(p.InstanceNumbers) == 1 {
		// Use legacy `--base_instance_num` when multi-vd is not requested.
		args = append(args, fmt.Sprintf("--base_instance_num=%d", p.InstanceNumbers[0]))
	} else {
		args = append(args, fmt.Sprintf("--num_instances=%s", strings.Join(SliceItoa(p.InstanceNumbers), ",")))
	}
	args = append(args, fmt.Sprintf("--system_image_dir=%s", p.MainArtifactsDir))
	if len(p.InstanceNumbers) > 1 {
		args = append(args, fmt.Sprintf("--num_instances=%d", len(p.InstanceNumbers)))
	}
	if p.KernelDir != "" {
		args = append(args, fmt.Sprintf("--kernel_path=%s/bzImage", p.KernelDir))
		initramfs := filepath.Join(p.KernelDir, "initramfs.img")
		if exist, _ := fileExist(initramfs); exist {
			args = append(args, "--initramfs_path="+initramfs)
		}
	}
	if p.BootloaderDir != "" {
		args = append(args, fmt.Sprintf("--bootloader=%s/u-boot.rom", p.BootloaderDir))
	}
	opts := cvdCommandOpts{
		AndroidHostOut: p.MainArtifactsDir,
		Home:           p.RuntimeDir,
		Timeout:        h.Timeout,
	}
	cvdCmd := newCVDCommand(h.ExecContext, h.CVDBin, args, opts)
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

const CVDCommandDefaultTimeout = 30 * time.Second

const (
	cvdUser              = "_cvd-executor"
	envVarAndroidHostOut = "ANDROID_HOST_OUT"
	envVarHome           = "HOME"
)

type cvdCommandOpts struct {
	AndroidHostOut string
	Home           string
	Stdout         io.Writer
	Timeout        time.Duration
}

type cvdCommand struct {
	execContext ExecContext
	cvdBin      string
	args        []string
	opts        cvdCommandOpts
}

func newCVDCommand(execContext ExecContext, cvdBin string, args []string, opts cvdCommandOpts) *cvdCommand {
	return &cvdCommand{
		execContext: execContext,
		cvdBin:      cvdBin,
		args:        args,
		opts:        opts,
	}
}

type cvdCommandExecErr struct {
	args   []string
	stderr string
	err    error
}

func (e *cvdCommandExecErr) Error() string {
	return fmt.Sprintf("cvd execution with args %q failed with stderr:\n%s",
		strings.Join(e.args, " "),
		e.stderr)
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
	// TODO: Use `context.WithTimeout` if upgrading to go 1.19 as `exec.Cmd` adds the `Cancel` function field,
	// so the cancel logic could be customized to continue sending the SIGINT signal.
	cmd := buildCvdCommand(context.TODO(), c.execContext, c.opts.AndroidHostOut, c.opts.Home, c.cvdBin, c.args...)
	stderr := &bytes.Buffer{}
	cmd.Stdout = c.opts.Stdout
	cmd.Stderr = stderr
	if err := cmd.Start(); err != nil {
		return err
	}
	var timedOut atomic.Value
	timedOut.Store(false)
	timeout := CVDCommandDefaultTimeout
	if c.opts.Timeout != 0 {
		timeout = c.opts.Timeout
	}
	go func() {
		select {
		case <-time.After(timeout):
			// NOTE: Do not use SIGKILL to terminate cvd commands. cvd commands are run using
			// `sudo` and contrary to SIGINT, SIGKILL is not relayed to child processes.
			if err := cmd.Process.Signal(syscall.SIGINT); err != nil {
				log.Printf("error sending SIGINT signal %+v", err)
			}
			timedOut.Store(true)
		}
	}()
	if err := cmd.Wait(); err != nil {
		logStderr(cmd, stderr.String())
		if timedOut.Load().(bool) {
			return &cvdCommandTimeoutErr{c.args}
		}
		return &cvdCommandExecErr{c.args, stderr.String(), err}
	}
	return nil
}

func (c *cvdCommand) startCVDServer() error {
	cmd := buildCvdCommand(context.TODO(), c.execContext, "", "", c.cvdBin)
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

func buildCvdCommand(ctx context.Context, execContext ExecContext, androidHostOut string, home string, cvdBin string, args ...string) *exec.Cmd {
	newArgs := []string{"-u", cvdUser, envVarHome + "=" + home}
	if androidHostOut != "" {
		newArgs = append(newArgs, envVarAndroidHostOut+"="+androidHostOut)
	}
	newArgs = append(newArgs, cvdBin)
	newArgs = append(newArgs, args...)
	return execContext(ctx, "sudo", newArgs...)
}

func cmdOutputLogMessage(output string) string {
	const format = "############################################\n" +
		"## BEGIN \n" +
		"############################################\n" +
		"\n%s\n\n" +
		"############################################\n" +
		"## END \n" +
		"############################################\n"
	return fmt.Sprintf(format, string(output))
}

func logStderr(cmd *exec.Cmd, val string) {
	msg := "`%s`, stderr:\n%s"
	log.Printf(msg, strings.Join(cmd.Args, " "), cmdOutputLogMessage(val))
}

func logCombinedStdoutStderr(cmd *exec.Cmd, val string) {
	msg := "`%s`, combined stdout and stderr :\n%s"
	log.Printf(msg, strings.Join(cmd.Args, " "), cmdOutputLogMessage(val))
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

func runAcloudSetup(execContext ExecContext, artifactsRootDir, artifactsDir, runtimeDir string) {
	run := func(cmd *exec.Cmd) {
		var b bytes.Buffer
		cmd.Stdout = &b
		cmd.Stderr = &b
		err := cmd.Run()
		if err != nil {
			log.Println("runAcloudSetup failed with error: " + b.String())
		}
	}
	// Creates symbolic link `acloud_link` which points to the passed device artifacts directory.
	go run(execContext(context.TODO(), "sudo", "-u", cvdUser, "ln", "-s", artifactsDir, artifactsRootDir+"/acloud_link"))
}

func SliceItoa(s []uint32) []string {
	result := make([]string, len(s))
	for i, v := range s {
		result[i] = strconv.Itoa(int(v))
	}
	return result
}

func nameToNumber(s string) (int, error) {
	// instance names follow format "cvd-[NUMBER]".
	parts := strings.Split(s, "-")
	if len(parts) != 2 {
		return 0, fmt.Errorf("failed parsing instance name %q", s)
	}
	n, err := strconv.Atoi(parts[1])
	if err != nil {
		return 0, fmt.Errorf("failed parsing instance name %q: %w", s, err)
	}
	return n, nil
}

func contains(s []uint32, e uint32) bool {
	for _, a := range s {
		if a == e {
			return true
		}
	}
	return false
}
