// Copyright 2024 Google LLC
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
	"log"
	"os/user"
	"path/filepath"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/cvd"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/exec"
	"github.com/google/android-cuttlefish/frontend/src/liboperator/operator"
)

type CreateSnapshotActionOpts struct {
	Selector         cvd.Selector
	Paths            IMPaths
	OperationManager OperationManager
	ExecContext      exec.ExecContext
	CVDUser          *user.User
}

type CreateSnapshotAction struct {
	selector cvd.Selector
	paths    IMPaths
	om       OperationManager
	cvdCLI   *cvd.CLI
}

func NewCreateSnapshotAction(opts CreateSnapshotActionOpts) *CreateSnapshotAction {
	return &CreateSnapshotAction{
		selector: opts.Selector,
		paths:    opts.Paths,
		om:       opts.OperationManager,
		cvdCLI:   cvd.NewCLI(exec.NewAsUserExecContext(opts.ExecContext, opts.CVDUser)),
	}
}

func (a *CreateSnapshotAction) Run() (apiv1.Operation, error) {
	if err := createDir(a.paths.SnapshotsRootDir); err != nil {
		return apiv1.Operation{}, err
	}
	op := a.om.New()
	go func(op apiv1.Operation) {
		result := &OperationResult{}
		result.Value, result.Error = a.createSnapshot(op)
		if err := a.om.Complete(op.Name, result); err != nil {
			log.Printf("error completing operation %q: %v\n", op.Name, err)
		}
	}(op)
	return op, nil
}

func (a *CreateSnapshotAction) createSnapshot(op apiv1.Operation) (*apiv1.CreateSnapshotResponse, error) {
	if err := a.cvdCLI.Suspend(a.selector); err != nil {
		return nil, operator.NewInternalError("cvd suspend failed", err)
	}
	snapshotID := op.Name
	dir := filepath.Join(a.paths.SnapshotsRootDir, snapshotID)
	if err := a.cvdCLI.TakeSnapshot(a.selector, dir); err != nil {
		return nil, operator.NewInternalError("cvd snapshot_take failed", err)
	}
	if err := a.cvdCLI.Resume(a.selector); err != nil {
		return nil, operator.NewInternalError("cvd resume failed", err)
	}
	return &apiv1.CreateSnapshotResponse{SnapshotID: snapshotID}, nil
}
