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
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/cvd"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/liboperator/api/v1"
)

type ListCVDsActionOpts struct {
	Paths           IMPaths
	ExecContext     ExecContext
	CVDToolsVersion AndroidBuild
	CVDDownloader   CVDDownloader
	CVDUser         string
}

type ListCVDsAction struct {
	paths           IMPaths
	cvdToolsVersion AndroidBuild
	cvdDownloader   CVDDownloader
	execContext     cvd.CVDExecContext
}

func NewListCVDsAction(opts ListCVDsActionOpts) *ListCVDsAction {
	return &ListCVDsAction{
		paths:           opts.Paths,
		cvdToolsVersion: opts.CVDToolsVersion,
		cvdDownloader:   opts.CVDDownloader,
		execContext:     newCVDExecContext(opts.ExecContext, opts.CVDUser),
	}
}

func (a *ListCVDsAction) Run() (*apiv1.ListCVDsResponse, error) {
	if err := a.cvdDownloader.Download(a.cvdToolsVersion, a.paths.CVDBin(), a.paths.FetchCVDBin()); err != nil {
		return nil, err
	}
	group, err := cvdFleetFirstGroup(a.execContext, a.paths.CVDBin())
	if err != nil {
		return nil, err
	}
	return &apiv1.ListCVDsResponse{CVDs: group.toAPIObject()}, nil
}
