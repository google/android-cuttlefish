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
	"log"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/artifacts"
	"github.com/google/android-cuttlefish/frontend/src/liboperator/operator"
)

type FetchArtifactsActionOpts struct {
	Request          *apiv1.FetchArtifactsRequest
	Paths            IMPaths
	OperationManager OperationManager
	BuildAPI         artifacts.BuildAPI
	CVDBundleFetcher artifacts.CVDBundleFetcher
	ArtifactsFetcher artifacts.Fetcher
	UUIDGen          func() string
}

type FetchArtifactsAction struct {
	req              *apiv1.FetchArtifactsRequest
	paths            IMPaths
	om               OperationManager
	buildAPI         artifacts.BuildAPI
	cvdBundleFetcher artifacts.CVDBundleFetcher
	artifactsFetcher artifacts.Fetcher
	artifactsMngr    *artifacts.Manager
}

func NewFetchArtifactsAction(opts FetchArtifactsActionOpts) *FetchArtifactsAction {
	return &FetchArtifactsAction{
		req:              opts.Request,
		paths:            opts.Paths,
		om:               opts.OperationManager,
		buildAPI:         opts.BuildAPI,
		cvdBundleFetcher: opts.CVDBundleFetcher,
		artifactsFetcher: opts.ArtifactsFetcher,

		artifactsMngr: artifacts.NewManager(opts.Paths.ArtifactsRootDir, opts.UUIDGen),
	}
}

func (a *FetchArtifactsAction) Run() (apiv1.Operation, error) {
	if err := validateFetchArtifactsRequest(a.req); err != nil {
		return apiv1.Operation{}, operator.NewBadRequestError("invalid FetchArtifactsRequest", err)
	}
	if err := createDir(a.paths.ArtifactsRootDir); err != nil {
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

	if req.AndroidCIBundle.Build == nil {
		req.AndroidCIBundle.Build = defaultMainBuild()
	}
	build := req.AndroidCIBundle.Build
	if build.BuildID == "" {
		if err := updateBuildsWithLatestGreenBuildID(a.buildAPI, []*apiv1.AndroidCIBuild{build}); err != nil {
			return OperationResult{
				Error: operator.NewInternalError("failed getting latest green build id", err),
			}
		}
	}
	switch t := req.AndroidCIBundle.Type; t {
	case apiv1.MainBundleType:
		_, err = a.artifactsMngr.GetCVDBundle(build.BuildID, build.Target, nil, a.cvdBundleFetcher)
	case apiv1.KernelBundleType:
		_, err = a.artifactsMngr.GetKernelBundle(build.BuildID, build.Target, a.artifactsFetcher)
	case apiv1.BootloaderBundleType:
		_, err = a.artifactsMngr.GetBootloaderBundle(build.BuildID, build.Target, a.artifactsFetcher)
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
