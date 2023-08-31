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
	"io/ioutil"
	"log"
	"sync"
	"sync/atomic"
	"time"

	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/artifacts"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/cvd"
	apiv1 "github.com/google/android-cuttlefish/frontend/src/liboperator/api/v1"
	"github.com/google/android-cuttlefish/frontend/src/liboperator/operator"

	"github.com/hashicorp/go-multierror"
)

type CreateCVDActionOpts struct {
	Request                  *apiv1.CreateCVDRequest
	HostValidator            Validator
	Paths                    IMPaths
	OperationManager         OperationManager
	ExecContext              ExecContext
	CVDDownloader            CVDDownloader
	CVDToolsVersion          AndroidBuild
	BuildAPI                 artifacts.BuildAPI
	ArtifactsFetcher         artifacts.Fetcher
	CVDBundleFetcher         artifacts.CVDBundleFetcher
	UUIDGen                  func() string
	CVDUser                  string
	CVDStartTimeout          time.Duration
	UserArtifactsDirResolver UserArtifactsDirResolver
}

type CreateCVDAction struct {
	req                      *apiv1.CreateCVDRequest
	hostValidator            Validator
	paths                    IMPaths
	om                       OperationManager
	execContext              cvd.CVDExecContext
	cvdToolsVersion          AndroidBuild
	cvdDownloader            CVDDownloader
	buildAPI                 artifacts.BuildAPI
	artifactsFetcher         artifacts.Fetcher
	cvdBundleFetcher         artifacts.CVDBundleFetcher
	userArtifactsDirResolver UserArtifactsDirResolver
	artifactsMngr            *artifacts.Manager
	startCVDHandler          *startCVDHandler
	cvdStartTimeout          time.Duration

	instanceCounter uint32
}

func NewCreateCVDAction(opts CreateCVDActionOpts) *CreateCVDAction {
	cvdExecContext := newCVDExecContext(opts.ExecContext, opts.CVDUser)
	return &CreateCVDAction{
		req:                      opts.Request,
		hostValidator:            opts.HostValidator,
		paths:                    opts.Paths,
		om:                       opts.OperationManager,
		cvdToolsVersion:          opts.CVDToolsVersion,
		cvdDownloader:            opts.CVDDownloader,
		buildAPI:                 opts.BuildAPI,
		artifactsFetcher:         opts.ArtifactsFetcher,
		cvdBundleFetcher:         opts.CVDBundleFetcher,
		userArtifactsDirResolver: opts.UserArtifactsDirResolver,
		cvdStartTimeout:          opts.CVDStartTimeout,

		artifactsMngr: artifacts.NewManager(
			opts.Paths.ArtifactsRootDir,
			opts.UUIDGen,
		),
		execContext: cvdExecContext,
		startCVDHandler: &startCVDHandler{
			ExecContext: cvdExecContext,
			CVDBin:      opts.Paths.CVDBin(),
			Timeout:     opts.CVDStartTimeout,
		},
	}
}

func (a *CreateCVDAction) Run() (apiv1.Operation, error) {
	if err := validateRequest(a.req); err != nil {
		return apiv1.Operation{}, operator.NewBadRequestError("invalid CreateCVDRequest", err)
	}
	if err := a.hostValidator.Validate(); err != nil {
		return apiv1.Operation{}, err
	}
	if err := createDir(a.paths.ArtifactsRootDir); err != nil {
		return apiv1.Operation{}, err
	}
	if err := createRuntimesRootDir(a.paths.RuntimesRootDir); err != nil {
		return apiv1.Operation{}, fmt.Errorf("failed creating cuttlefish runtime directory: %w", err)
	}
	if err := a.cvdDownloader.Download(a.cvdToolsVersion, a.paths.CVDBin(), a.paths.FetchCVDBin()); err != nil {
		return apiv1.Operation{}, err
	}
	op := a.om.New()
	go a.launchCVD(op)
	return op, nil
}

func (a *CreateCVDAction) launchCVD(op apiv1.Operation) {
	result := &OperationResult{}
	if a.req.EnvConfig != nil {
		result.Value, result.Error = a.launchWithCanonicalConfig(op)
	} else {
		result = a.launchCVDResult(op)
	}
	if err := a.om.Complete(op.Name, result); err != nil {
		log.Printf("error completing launch cvd operation %q: %v\n", op.Name, err)
	}
}

func (a *CreateCVDAction) launchWithCanonicalConfig(op apiv1.Operation) (*apiv1.CreateCVDResponse, error) {
	data, err := json.MarshalIndent(a.req.EnvConfig, "", " ")
	if err != nil {
		return nil, err
	}
	file, err := ioutil.TempFile("", "cvdload*.json")
	if err != nil {
		return nil, err
	}
	if err := file.Chmod(0640); err != nil {
		return nil, err
	}
	_, err = file.Write(data)
	if closeErr := file.Close(); closeErr != nil && err == nil {
		err = closeErr
	}
	if err != nil {
		return nil, err
	}
	args := []string{"load", file.Name()}
	opts := cvd.CommandOpts{
		Timeout: a.cvdStartTimeout,
	}
	cmd := cvd.NewCommand(a.execContext, a.paths.CVDBin(), args, opts)
	if err := cmd.Run(); err != nil {
		return nil, operator.NewInternalError(ErrMsgLaunchCVDFailed, err)
	}
	fleet, err := getCVDFleet(a.execContext, a.paths.CVDBin())
	if err != nil {
		return nil, err
	}
	return &apiv1.CreateCVDResponse{CVDs: fleetToCVDs(fleet)}, nil
}

func (a *CreateCVDAction) launchCVDResult(op apiv1.Operation) *OperationResult {
	instancesCount := 1 + a.req.AdditionalInstancesNum
	var err error
	switch {
	case a.req.CVD.BuildSource.AndroidCIBuildSource != nil:
		_, err = a.launchFromAndroidCI(a.req.CVD.BuildSource.AndroidCIBuildSource, instancesCount, op)
	case a.req.CVD.BuildSource.UserBuildSource != nil:
		_, err = a.launchFromUserBuild(a.req.CVD.BuildSource.UserBuildSource, instancesCount, op)
	default:
		return &OperationResult{
			Error: operator.NewBadRequestError(
				"Invalid CreateCVDRequest, missing BuildSource information.", nil),
		}
	}
	if err != nil {
		return &OperationResult{Error: operator.NewInternalError(ErrMsgLaunchCVDFailed, err)}
	}
	fleet, err := getCVDFleet(a.execContext, a.paths.CVDBin())
	if err != nil {
		return &OperationResult{Error: operator.NewInternalError(ErrMsgLaunchCVDFailed, err)}
	}
	res := &apiv1.CreateCVDResponse{
		CVDs: fleetToCVDs(fleet),
	}
	return &OperationResult{Value: res}
}

const ErrMsgLaunchCVDFailed = "failed to launch cvd"

func (a *CreateCVDAction) launchFromAndroidCI(
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
	if err := updateBuildsWithLatestGreenBuildID(a.buildAPI,
		[]*apiv1.AndroidCIBuild{mainBuild, kernelBuild, bootloaderBuild, systemImgBuild}); err != nil {
		return nil, err
	}
	var mainBuildDir, kernelBuildDir, bootloaderBuildDir string
	var mainBuildErr, kernelBuildErr, bootloaderBuildErr error
	var wg sync.WaitGroup
	wg.Add(1)
	go func() {
		defer wg.Done()
		var extraCVDOptions *artifacts.ExtraCVDOptions = nil
		if systemImgBuild != nil {
			extraCVDOptions = &artifacts.ExtraCVDOptions{
				SystemImgBuildID: systemImgBuild.BuildID,
				SystemImgTarget:  systemImgBuild.Target,
			}
		}
		mainBuildDir, mainBuildErr = a.artifactsMngr.GetCVDBundle(
			mainBuild.BuildID, mainBuild.Target, extraCVDOptions, a.cvdBundleFetcher)
	}()
	if kernelBuild != nil {
		wg.Add(1)
		go func() {
			defer wg.Done()
			kernelBuildDir, kernelBuildErr = a.artifactsMngr.GetKernelBundle(
				kernelBuild.BuildID, kernelBuild.Target, a.artifactsFetcher)
		}()
	}
	if bootloaderBuild != nil {
		wg.Add(1)
		go func() {
			defer wg.Done()
			bootloaderBuildDir, bootloaderBuildErr = a.artifactsMngr.GetBootloaderBundle(
				bootloaderBuild.BuildID, bootloaderBuild.Target, a.artifactsFetcher)
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
		InstanceNumbers:  a.newInstanceNumbers(instancesCount),
		MainArtifactsDir: mainBuildDir,
		RuntimeDir:       a.paths.RuntimesRootDir,
		KernelDir:        kernelBuildDir,
		BootloaderDir:    bootloaderBuildDir,
	}
	if err := a.startCVDHandler.Start(startParams); err != nil {
		return nil, err
	}
	// TODO: Remove once `acloud CLI` gets deprecated.
	if contains(startParams.InstanceNumbers, 1) {
		go runAcloudSetup(a.execContext, a.paths.ArtifactsRootDir, mainBuildDir, a.paths.RuntimesRootDir)
	}
	return startParams.InstanceNumbers, nil
}

func (a *CreateCVDAction) launchFromUserBuild(
	buildSource *apiv1.UserBuildSource, instancesCount uint32, op apiv1.Operation) ([]uint32, error) {
	artifactsDir := a.userArtifactsDirResolver.GetDirPath(buildSource.ArtifactsDir)
	if err := untarCVDHostPackage(artifactsDir); err != nil {
		return nil, err
	}
	startParams := startCVDParams{
		InstanceNumbers:  a.newInstanceNumbers(instancesCount),
		MainArtifactsDir: artifactsDir,
		RuntimeDir:       a.paths.RuntimesRootDir,
	}
	if err := a.startCVDHandler.Start(startParams); err != nil {
		return nil, err
	}
	// TODO: Remove once `acloud CLI` gets deprecated.
	if contains(startParams.InstanceNumbers, 1) {
		go runAcloudSetup(a.execContext, a.paths.ArtifactsRootDir, artifactsDir, a.paths.RuntimesRootDir)
	}
	return startParams.InstanceNumbers, nil
}

func (a *CreateCVDAction) newInstanceNumbers(n uint32) []uint32 {
	result := []uint32{}
	for i := 0; i < int(n); i++ {
		num := atomic.AddUint32(&a.instanceCounter, 1)
		result = append(result, num)
	}
	return result
}

func validateRequest(r *apiv1.CreateCVDRequest) error {
	if r.EnvConfig != nil {
		return nil
	}
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
