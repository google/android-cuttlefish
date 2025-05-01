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
	"bytes"
	"encoding/json"
	"io/ioutil"
	"log"
	"os"
	"os/user"
	"sync"
	"sync/atomic"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/artifacts"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/cvd"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/exec"
	"github.com/google/android-cuttlefish/frontend/src/liboperator/operator"

	"github.com/hashicorp/go-multierror"
)

type BuildAPICredentials struct {
	AccessToken string
	// The credential for exchanging access tokens should be generated from a GCP project that
	// has the Build API enabled. If it isn't, UserProjectID is required for successful API usage.
	// The value of UserProjectID is expected to be the project ID of a GCP project that has the
	// Build API enabled. This project ID can differ from the one used to generate OAuth credentials.
	UserProjectID string
}

type CreateCVDActionOpts struct {
	Request                  *apiv1.CreateCVDRequest
	HostValidator            Validator
	Paths                    IMPaths
	OperationManager         OperationManager
	ExecContext              exec.ExecContext
	BuildAPI                 artifacts.BuildAPI
	ArtifactsFetcher         artifacts.Fetcher
	CVDBundleFetcher         artifacts.CVDBundleFetcher
	UUIDGen                  func() string
	CVDUser                  *user.User
	UserArtifactsDirResolver UserArtifactsDirResolver
	BuildAPICredentials      BuildAPICredentials
}

type CreateCVDAction struct {
	req                      *apiv1.CreateCVDRequest
	hostValidator            Validator
	paths                    IMPaths
	om                       OperationManager
	execContext              exec.ExecContext
	cvdCLI                   *cvd.CLI
	buildAPI                 artifacts.BuildAPI
	artifactsFetcher         artifacts.Fetcher
	cvdBundleFetcher         artifacts.CVDBundleFetcher
	userArtifactsDirResolver UserArtifactsDirResolver
	artifactsMngr            *artifacts.Manager
	cvdUser                  *user.User
	buildAPICredentials      BuildAPICredentials

	instanceCounter uint32
}

func NewCreateCVDAction(opts CreateCVDActionOpts) *CreateCVDAction {
	execCtx := exec.NewAsUserExecContext(opts.ExecContext, opts.CVDUser)
	return &CreateCVDAction{
		req:                      opts.Request,
		hostValidator:            opts.HostValidator,
		paths:                    opts.Paths,
		om:                       opts.OperationManager,
		buildAPI:                 opts.BuildAPI,
		artifactsFetcher:         opts.ArtifactsFetcher,
		cvdBundleFetcher:         opts.CVDBundleFetcher,
		userArtifactsDirResolver: opts.UserArtifactsDirResolver,
		cvdUser:                  opts.CVDUser,
		buildAPICredentials:      opts.BuildAPICredentials,

		artifactsMngr: artifacts.NewManager(
			opts.Paths.ArtifactsRootDir,
			opts.UUIDGen,
		),
		execContext: execCtx,
		cvdCLI:      cvd.NewCLI(execCtx),
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
	log.Printf("environment config:\n%s", string(data))
	data = bytes.ReplaceAll(data,
		[]byte(apiv1.EnvConfigUserArtifactsVar+"/"),
		[]byte(a.userArtifactsDirResolver.GetDirPath("")))
	configFile, err := createTempFile("cvdload*.json", data, 0640)
	if err != nil {
		return nil, err
	}

	var creds cvd.FetchCredentials
	if a.buildAPICredentials.AccessToken != "" {
		creds = &cvd.FetchTokenFileCredentials{
			AccessToken: a.buildAPICredentials.AccessToken,
			ProjectId:   a.buildAPICredentials.UserProjectID,
		}
	} else {
		log.Printf("fetch credentials: no access token provided by client")
		if isRunningOnGCE() {
			log.Println("fetch credentials: running on gce")
			if ok, err := hasServiceAccountAccessToken(); err != nil {
				log.Printf("fetch credentials: service account token check failed: %s", err)
			} else if ok {
				log.Println("fetch credentials: using gce service account credentials")
				creds = &cvd.FetchGceCredentials{}
			}
		}
	}

	group, err := a.cvdCLI.Load(configFile.Name(), creds)
	if err != nil {
		return nil, operator.NewInternalError(ErrMsgLaunchCVDFailed, err)
	}
	return &apiv1.CreateCVDResponse{CVDs: CvdGroupToAPIObject(group)}, nil
}

func (a *CreateCVDAction) launchCVDResult(op apiv1.Operation) *OperationResult {
	instancesCount := 1 + a.req.AdditionalInstancesNum
	var group *cvd.Group
	var err error
	switch {
	case a.req.CVD.BuildSource.AndroidCIBuildSource != nil:
		group, err = a.launchFromAndroidCI(a.req.CVD.BuildSource.AndroidCIBuildSource, instancesCount, op)
	default:
		return &OperationResult{
			Error: operator.NewBadRequestError(
				"Invalid CreateCVDRequest, missing BuildSource information.", nil),
		}
	}
	if err != nil {
		return &OperationResult{Error: operator.NewInternalError(ErrMsgLaunchCVDFailed, err)}
	}
	res := &apiv1.CreateCVDResponse{CVDs: CvdGroupToAPIObject(group)}
	return &OperationResult{Value: res}
}

const ErrMsgLaunchCVDFailed = "failed to launch cvd"

func (a *CreateCVDAction) launchFromAndroidCI(
	buildSource *apiv1.AndroidCIBuildSource, instancesCount uint32, op apiv1.Operation) (*cvd.Group, error) {
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
		KernelDir:        kernelBuildDir,
		BootloaderDir:    bootloaderBuildDir,
	}
	group, err := CreateCVD(a.execContext, startParams)
	if err != nil {
		return nil, err
	}
	// TODO: Remove once `acloud CLI` gets deprecated.
	if contains(startParams.InstanceNumbers, 1) {
		go runAcloudSetup(a.execContext, a.paths.ArtifactsRootDir, mainBuildDir)
	}
	return group, nil
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
	if r.CVD.BuildSource.AndroidCIBuildSource == nil {
		return EmptyFieldError("BuildSource")
	}
	return nil
}

// See https://pkg.go.dev/io/ioutil@go1.13.15#TempFile
func createTempFile(pattern string, data []byte, mode os.FileMode) (*os.File, error) {
	file, err := ioutil.TempFile("", pattern)
	if err != nil {
		return nil, err
	}
	if err := file.Chmod(mode); err != nil {
		return nil, err
	}
	_, err = file.Write(data)
	if closeErr := file.Close(); closeErr != nil && err == nil {
		err = closeErr
	}
	if err != nil {
		return nil, err
	}
	return file, nil
}
