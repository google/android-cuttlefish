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
	"os"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	"github.com/google/android-cuttlefish/frontend/src/liboperator/operator"
)

type FetchArtifactsActionOpts struct {
	Request          *apiv1.FetchArtifactsRequest
	Paths            IMPaths
	OperationManager OperationManager
	CVDBundleFetcher CVDBundleFetcher
	UUIDGen          func() string
}

type FetchArtifactsAction struct {
	req              *apiv1.FetchArtifactsRequest
	paths            IMPaths
	om               OperationManager
	cvdBundleFetcher CVDBundleFetcher
}

func NewFetchArtifactsAction(opts FetchArtifactsActionOpts) *FetchArtifactsAction {
	return &FetchArtifactsAction{
		req:              opts.Request,
		paths:            opts.Paths,
		om:               opts.OperationManager,
		cvdBundleFetcher: opts.CVDBundleFetcher,
	}
}

func (a *FetchArtifactsAction) Run() (apiv1.Operation, error) {
	if err := validateFetchArtifactsRequest(a.req); err != nil {
		return apiv1.Operation{}, operator.NewBadRequestError("invalid FetchArtifactsRequest", err)
	}
	if err := createDir(a.paths.InstancesDir); err != nil {
		return apiv1.Operation{}, err
	}
	op := a.om.New()
	go func() {
		result := a.startDownload(op)
		if err := a.om.Complete(op.Name, &result); err != nil {
			log.Printf("error completing fetch artifacts operation %q: %v\n", op.Name, err)
		}
	}()
	return op, nil
}

const errMsgFailedFetchingArtifacts = "failed fetching artifacts"

func (a *FetchArtifactsAction) startDownload(op apiv1.Operation) OperationResult {
	req, err := cloneRequest(a.req)
	if err != nil {
		return OperationResult{
			Error: operator.NewInternalError("error cloning request", err),
		}
	}
	dir, err := ioutil.TempDir(a.paths.InstancesDir, "ins*")
	if err != nil {
		return OperationResult{Error: operator.NewInternalError("error creating tmp dir", err)}
	}
	// Let `cvd fetch` create the directory.
	if err := os.RemoveAll(dir); err != nil {
		return OperationResult{Error: operator.NewInternalError("error removing tmp dir", err)}
	}
	defer func() {
		// Remove directory, original artifacts remains in cache at /tmp/cvd/$USER/cache
		if err := os.RemoveAll(dir); err != nil {
			log.Printf("error removing directory %q: %s", dir, err)
		}
	}()
	build := req.AndroidCIBundle.Build
	switch t := req.AndroidCIBundle.Type; t {
	case apiv1.MainBundleType:
		err = a.cvdBundleFetcher.Fetch(dir, build.BuildID, build.Target, ExtraCVDOptions{})
	case apiv1.KernelBundleType:
	case apiv1.BootloaderBundleType:
		// Do not fail due backwards compatibility. If artifact does not exist, the
		// follow up create request is going to fail.
		err = nil
	default:
		err = operator.NewBadRequestError(fmt.Sprintf("Unsupported artifact bundle type: %d", t), nil)
	}
	if err != nil {
		return OperationResult{Error: operator.NewInternalError(errMsgFailedFetchingArtifacts, err)}
	}
	res := &apiv1.FetchArtifactsResponse{
		AndroidCIBundle: req.AndroidCIBundle,
	}
	return OperationResult{Value: res}
}

func validateFetchArtifactsRequest(r *apiv1.FetchArtifactsRequest) error {
	if r.AndroidCIBundle == nil {
		return EmptyFieldError("AndroidCIBundle")
	}
	if r.AndroidCIBundle.Type > 0 && r.AndroidCIBundle.Build == nil {
		return EmptyFieldError("AndroidCIBundle.Build")
	}
	return nil
}

func cloneRequest(r *apiv1.FetchArtifactsRequest) (*apiv1.FetchArtifactsRequest, error) {
	data, err := json.Marshal(r)
	if err != nil {
		return nil, err
	}
	res := &apiv1.FetchArtifactsRequest{}
	if err := json.Unmarshal(data, res); err != nil {
		return nil, err
	}
	return res, nil
}
