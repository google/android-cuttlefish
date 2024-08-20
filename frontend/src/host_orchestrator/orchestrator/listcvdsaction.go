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
	"os/user"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/cvd"
	"github.com/google/android-cuttlefish/frontend/src/liboperator/operator"
)

type ListCVDsActionOpts struct {
	Group       string
	Paths       IMPaths
	ExecContext ExecContext
	CVDUser     *user.User
}

type ListCVDsAction struct {
	group       string
	paths       IMPaths
	execContext cvd.CVDExecContext
}

func NewListCVDsAction(opts ListCVDsActionOpts) *ListCVDsAction {
	return &ListCVDsAction{
		group:       opts.Group,
		paths:       opts.Paths,
		execContext: newCVDExecContext(opts.ExecContext, opts.CVDUser),
	}
}

func (a *ListCVDsAction) Run() (*apiv1.ListCVDsResponse, error) {
	fleet, err := cvdFleet(a.execContext)
	if err != nil {
		return nil, err
	}
	groups := fleet.Groups
	if a.group != "" {
		ok, g := fleet.findGroup(a.group)
		if !ok {
			return nil, operator.NewNotFoundError(fmt.Sprintf("Group %q not found", a.group), nil)
		}
		groups = []*cvdGroup{g}
	}
	cvds := []*apiv1.CVD{}
	for _, g := range groups {
		cvds = append(cvds, g.toAPIObject()...)
	}
	return &apiv1.ListCVDsResponse{CVDs: cvds}, nil
}
