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
	"path/filepath"

	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/cvd"
	apiv1 "github.com/google/android-cuttlefish/frontend/src/liboperator/api/v1"
	"github.com/google/android-cuttlefish/frontend/src/liboperator/operator"
)

type CreateCVDBugReportActionOpts struct {
	Group            string
	Paths            IMPaths
	OperationManager OperationManager
	ExecContext      cvd.CVDExecContext
	UUIDGen          func() string
}

type CreateCVDBugReportAction struct {
	group       string
	paths       IMPaths
	om          OperationManager
	execContext cvd.CVDExecContext
	uuidgen     func() string
}

func NewCreateCVDBugReportAction(opts CreateCVDBugReportActionOpts) *CreateCVDBugReportAction {
	return &CreateCVDBugReportAction{
		group:       opts.Group,
		paths:       opts.Paths,
		om:          opts.OperationManager,
		execContext: opts.ExecContext,
		uuidgen:     opts.UUIDGen,
	}
}

func (a *CreateCVDBugReportAction) Run() (apiv1.Operation, error) {
	if a.group == "" {
		return apiv1.Operation{}, operator.NewBadRequestError("empty group", nil)
	}
	if err := createDir(a.paths.CVDBugReportsDir); err != nil {
		return apiv1.Operation{}, err
	}
	uuid := a.uuidgen()
	if err := createNewDir(filepath.Join(a.paths.CVDBugReportsDir, uuid)); err != nil {
		return apiv1.Operation{}, err
	}
	op := a.om.New()
	go a.createBugReport(uuid, a.group, op)
	return op, nil
}

const BugReportZipFileName = "cvd_bugreport.zip"

func (a *CreateCVDBugReportAction) createBugReport(uuid, group string, op apiv1.Operation) {
	result := &OperationResult{}
	filename := filepath.Join(a.paths.CVDBugReportsDir, uuid, "cvd_bugreport.zip")
	sel := &CVDSelector{Group: group}
	args := sel.ToCVDCLI()
	args = append(args, []string{"host_bugreport", "--output=" + filename}...)
	cmd := cvd.NewCommand(a.execContext, args, cvd.CommandOpts{})
	if err := cmd.Run(); err != nil {
		result.Error = operator.NewInternalError("`cvd host_bugreport` failed: ", err)
	} else {
		result.Value = uuid
	}
	if err := a.om.Complete(op.Name, result); err != nil {
		log.Printf("error completing operation %q: %v", op.Name, err)
	}
}
