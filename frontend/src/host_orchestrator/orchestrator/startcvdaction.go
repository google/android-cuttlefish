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
	"path/filepath"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/cvd"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/exec"
	"github.com/google/android-cuttlefish/frontend/src/liboperator/operator"
)

type StartCVDActionOpts struct {
	Request          *apiv1.StartCVDRequest
	Selector         cvd.GroupSelector
	Paths            IMPaths
	OperationManager OperationManager
	ExecContext      exec.ExecContext
}

type StartCVDAction struct {
	req      *apiv1.StartCVDRequest
	selector cvd.GroupSelector
	paths    IMPaths
	om       OperationManager
	cvdCLI   *cvd.CLI
}

func NewStartCVDAction(opts StartCVDActionOpts) *StartCVDAction {
	return &StartCVDAction{
		req:      opts.Request,
		selector: opts.Selector,
		paths:    opts.Paths,
		om:       opts.OperationManager,
		cvdCLI:   cvd.NewCLI(opts.ExecContext),
	}
}

func (a *StartCVDAction) Run() (apiv1.Operation, error) {
	if err := ValidateStartCVDRequest(a.req); err != nil {
		return apiv1.Operation{}, err
	}
	opts := cvd.StartOptions{}
	if a.req.SnapshotID != "" {
		path := filepath.Join(a.paths.SnapshotsRootDir, a.req.SnapshotID)
		if ok, err := fileExist(path); !ok {
			if err != nil {
				return apiv1.Operation{}, err
			}
			return apiv1.Operation{}, operator.NewNotFoundError(fmt.Sprintf("snapshot id %q not found", a.req.SnapshotID), nil)
		}
		opts.SnapshotPath = path
	}
	op := a.om.New()
	go func(op apiv1.Operation) {
		result := &OperationResult{}
		result.Value, result.Error = a.exec(op, opts)
		if err := a.om.Complete(op.Name, result); err != nil {
			log.Printf("error completing operation %q: %v\n", op.Name, err)
		}
	}(op)
	return op, nil
}

func (a *StartCVDAction) exec(op apiv1.Operation, opts cvd.StartOptions) (*apiv1.EmptyResponse, error) {
	if err := a.cvdCLI.LazySelectGroup(a.selector).Start(opts); err != nil {
		return nil, operator.NewInternalError("cvd start failed", err)
	}
	return &apiv1.EmptyResponse{}, nil
}
