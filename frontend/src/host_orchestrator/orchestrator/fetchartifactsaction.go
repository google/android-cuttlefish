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
	"fmt"
	"log"

	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/artifacts"
	apiv1 "github.com/google/android-cuttlefish/frontend/src/liboperator/api/v1"
	"github.com/google/android-cuttlefish/frontend/src/liboperator/operator"
)

type FetchArtifactsActionOpts struct {
	Request          *apiv1.FetchArtifactsRequest
	Paths            IMPaths
	CVDToolsVersion  AndroidBuild
	CVDDownloader    CVDDownloader
	OperationManager OperationManager
	CVDBundleFetcher artifacts.CVDBundleFetcher
	ArtifactsFetcher artifacts.Fetcher
	UUIDGen          func() string
}

type FetchArtifactsAction struct {
	req              *apiv1.FetchArtifactsRequest
	paths            IMPaths
	cvdToolsVersion  AndroidBuild
	cvdDownloader    CVDDownloader
	om               OperationManager
	cvdBundleFetcher artifacts.CVDBundleFetcher
	artifactsFetcher artifacts.Fetcher
	artifactsMngr    *artifacts.Manager
}

func NewFetchArtifactsAction(opts FetchArtifactsActionOpts) *FetchArtifactsAction {
	return &FetchArtifactsAction{
		req:              opts.Request,
		paths:            opts.Paths,
		cvdToolsVersion:  opts.CVDToolsVersion,
		cvdDownloader:    opts.CVDDownloader,
		om:               opts.OperationManager,
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
	if err := a.cvdDownloader.Download(a.cvdToolsVersion, a.paths.CVDBin(), a.paths.FetchCVDBin()); err != nil {
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
	build := defaultMainBuild()
	if a.req.AndroidCIBundle.Build != nil {
		build = a.req.AndroidCIBundle.Build
	}
	var err error
	switch t := a.req.AndroidCIBundle.Type; t {
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
	return OperationResult{}
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
