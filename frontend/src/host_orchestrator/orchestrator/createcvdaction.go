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
	"io/ioutil"
	"log"
	"os"
	"strings"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/cvd"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/exec"
	"github.com/google/android-cuttlefish/frontend/src/liboperator/operator"
)

type CreateCVDActionOpts struct {
	Request                  *apiv1.CreateCVDRequest
	HostValidator            Validator
	Paths                    IMPaths
	OperationManager         OperationManager
	ExecContext              exec.ExecContext
	CVDBundleFetcher         CVDBundleFetcher
	UUIDGen                  func() string
	UserArtifactsDirResolver UserArtifactsDirResolver
	FetchCredentials         cvd.FetchCredentials
	BuildAPIBaseURL          string
}

type CreateCVDAction struct {
	req                      *apiv1.CreateCVDRequest
	hostValidator            Validator
	paths                    IMPaths
	om                       OperationManager
	execContext              exec.ExecContext
	cvdCLI                   *cvd.CLI
	cvdBundleFetcher         CVDBundleFetcher
	userArtifactsDirResolver UserArtifactsDirResolver
	fetchCredentials         cvd.FetchCredentials
	buildAPIBaseURL          string
}

func NewCreateCVDAction(opts CreateCVDActionOpts) *CreateCVDAction {
	execCtx := opts.ExecContext
	return &CreateCVDAction{
		req:                      opts.Request,
		hostValidator:            opts.HostValidator,
		paths:                    opts.Paths,
		om:                       opts.OperationManager,
		cvdBundleFetcher:         opts.CVDBundleFetcher,
		userArtifactsDirResolver: opts.UserArtifactsDirResolver,
		fetchCredentials:         opts.FetchCredentials,
		buildAPIBaseURL:          opts.BuildAPIBaseURL,
		execContext:              execCtx,
		cvdCLI:                   cvd.NewCLI(execCtx),
	}
}

func (a *CreateCVDAction) Run() (apiv1.Operation, error) {
	if err := validateRequest(a.req); err != nil {
		return apiv1.Operation{}, operator.NewBadRequestError("invalid CreateCVDRequest", err)
	}
	if err := a.hostValidator.Validate(); err != nil {
		return apiv1.Operation{}, err
	}
	if err := createDir(a.paths.InstancesDir); err != nil {
		return apiv1.Operation{}, err
	}
	op := a.om.New()
	go a.launchCVD(op)
	return op, nil
}

func (a *CreateCVDAction) launchCVD(op apiv1.Operation) {
	result := &OperationResult{}
	result.Value, result.Error = a.launchWithCanonicalConfig(op)
	if err := a.om.Complete(op.Name, result); err != nil {
		log.Printf("error completing launch cvd operation %q: %v\n", op.Name, err)
	}
}

func (a *CreateCVDAction) launchWithCanonicalConfig(op apiv1.Operation) (*apiv1.CreateCVDResponse, error) {
	data, err := json.MarshalIndent(a.req.EnvConfig, "", " ")
	if err != nil {
		return nil, err
	}
	config := string(data)
	log.Printf("environment config:\n%s", config)
	config = strings.ReplaceAll(config,
		apiv1.EnvConfigImageDirectoriesVar+"/",
		a.paths.ImageDirectoriesDir+"/")
	configFile, err := createTempFile("cvdload*.json", config, 0640)
	if err != nil {
		return nil, err
	}
	opts := cvd.LoadOpts{
		BuildAPIBaseURL: a.buildAPIBaseURL,
		Credentials:     a.fetchCredentials,
	}
	group, err := a.cvdCLI.Load(configFile.Name(), opts)
	if err != nil {
		return nil, operator.NewInternalError(ErrMsgLaunchCVDFailed, err)
	}
	return &apiv1.CreateCVDResponse{CVDs: CvdGroupToAPIObject(group)}, nil
}

const ErrMsgLaunchCVDFailed = "failed to launch cvd"

func validateRequest(r *apiv1.CreateCVDRequest) error {
	if r.EnvConfig == nil {
		return EmptyFieldError("EnvConfig")
	}
	return nil
}

// See https://pkg.go.dev/io/ioutil@go1.13.15#TempFile
func createTempFile(pattern string, data string, mode os.FileMode) (*os.File, error) {
	file, err := ioutil.TempFile("", pattern)
	if err != nil {
		return nil, err
	}
	if err := file.Chmod(mode); err != nil {
		return nil, err
	}
	_, err = file.WriteString(data)
	if closeErr := file.Close(); closeErr != nil && err == nil {
		err = closeErr
	}
	if err != nil {
		return nil, err
	}
	return file, nil
}
