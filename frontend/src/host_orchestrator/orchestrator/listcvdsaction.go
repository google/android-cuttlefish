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

	apiv1 "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/cvd"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/exec"
	"github.com/google/android-cuttlefish/frontend/src/liboperator/operator"
)

type ListCVDsActionOpts struct {
	Group       string
	Paths       IMPaths
	ExecContext exec.ExecContext
}

type ListCVDsAction struct {
	group  string
	paths  IMPaths
	cvdCLI *cvd.CLI
}

func NewListCVDsAction(opts ListCVDsActionOpts) *ListCVDsAction {
	return &ListCVDsAction{
		group:  opts.Group,
		paths:  opts.Paths,
		cvdCLI: cvd.NewCLI(opts.ExecContext),
	}
}

func (a *ListCVDsAction) Run() (*apiv1.ListCVDsResponse, error) {
	groups, err := a.cvdCLI.Fleet()
	if err != nil {
		return nil, err
	}
	if a.group != "" {
		ok, g := findGroup(groups, a.group)
		if !ok {
			return nil, operator.NewNotFoundError(fmt.Sprintf("Group %q not found", a.group), nil)
		}
		groups = []*cvd.Group{g}
	}
	cvds := []*apiv1.CVD{}
	for _, g := range groups {
		cvds = append(cvds, CvdGroupToAPIObject(g)...)
	}
	return &apiv1.ListCVDsResponse{CVDs: cvds}, nil
}
