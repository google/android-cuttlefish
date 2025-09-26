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
	"log"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/cvd"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/exec"
)

type execCvdGroupCommand interface {
	exec(cvd *cvd.CLI, sel cvd.GroupSelector) error
}

type startCvdCommand struct{}

func (a *startCvdCommand) exec(cvdCLI *cvd.CLI, sel cvd.GroupSelector) error {
	return cvdCLI.LazySelectGroup(sel).Start(cvd.StartOptions{})
}

type stopCvdCommand struct{}

func (a *stopCvdCommand) exec(cvd *cvd.CLI, sel cvd.GroupSelector) error {
	return cvd.LazySelectGroup(sel).Stop()
}

type removeCvdCommand struct{}

func (a *removeCvdCommand) exec(cvd *cvd.CLI, sel cvd.GroupSelector) error {
	return cvd.LazySelectGroup(sel).Remove()
}

type ExecCVDGroupCommandActionOpts struct {
	Command          execCvdGroupCommand
	Selector         cvd.GroupSelector
	OperationManager OperationManager
	ExecContext      exec.ExecContext
}

type ExecCVDGroupCommandAction struct {
	command  execCvdGroupCommand
	selector cvd.GroupSelector
	om       OperationManager
	cvdCLI   *cvd.CLI
}

func NewExecCVDGroupCommandAction(opts ExecCVDGroupCommandActionOpts) *ExecCVDGroupCommandAction {
	return &ExecCVDGroupCommandAction{
		command:  opts.Command,
		selector: opts.Selector,
		om:       opts.OperationManager,
		cvdCLI:   cvd.NewCLI(opts.ExecContext),
	}
}

func (a *ExecCVDGroupCommandAction) Run() (apiv1.Operation, error) {
	op := a.om.New()
	go func(op apiv1.Operation) {
		result := &OperationResult{}
		result.Value = &apiv1.EmptyResponse{}
		result.Error = a.command.exec(a.cvdCLI, a.selector)
		if err := a.om.Complete(op.Name, result); err != nil {
			log.Printf("error completing operation %q: %v\n", op.Name, err)
		}
	}(op)
	return op, nil
}

type execCvdInstanceCommand interface {
	exec(cvd *cvd.CLI, sel cvd.InstanceSelector) error
}

type powerwashCvdCommand struct{}

func (a *powerwashCvdCommand) exec(cvd *cvd.CLI, sel cvd.InstanceSelector) error {
	return cvd.LazySelectInstance(sel).PowerWash()
}

type powerbtnCvdCommand struct{}

func (a *powerbtnCvdCommand) exec(cvd *cvd.CLI, sel cvd.InstanceSelector) error {
	return cvd.LazySelectInstance(sel).PowerBtn()
}

type startScreenRecordingCvdCommand struct{}

func (a *startScreenRecordingCvdCommand) exec(cvd *cvd.CLI, sel cvd.InstanceSelector) error {
	return cvd.LazySelectInstance(sel).StartScreenRecording()
}

type stopScreenRecordingCvdCommand struct{}

func (a *stopScreenRecordingCvdCommand) exec(cvd *cvd.CLI, sel cvd.InstanceSelector) error {
	return cvd.LazySelectInstance(sel).StopScreenRecording()
}

type ExecCVDInstanceCommandActionOpts struct {
	Command          execCvdInstanceCommand
	Selector         cvd.InstanceSelector
	OperationManager OperationManager
	ExecContext      exec.ExecContext
}

type ExecCVDInstanceCommandAction struct {
	command  execCvdInstanceCommand
	selector cvd.InstanceSelector
	om       OperationManager
	cvdCLI   *cvd.CLI
}

func NewExecCVDInstanceCommandAction(opts ExecCVDInstanceCommandActionOpts) *ExecCVDInstanceCommandAction {
	return &ExecCVDInstanceCommandAction{
		command:  opts.Command,
		selector: opts.Selector,
		om:       opts.OperationManager,
		cvdCLI:   cvd.NewCLI(opts.ExecContext),
	}
}

func (a *ExecCVDInstanceCommandAction) Run() (apiv1.Operation, error) {
	op := a.om.New()
	go func(op apiv1.Operation) {
		result := &OperationResult{}
		result.Value = &apiv1.EmptyResponse{}
		result.Error = a.command.exec(a.cvdCLI, a.selector)
		if err := a.om.Complete(op.Name, result); err != nil {
			log.Printf("error completing operation %q: %v\n", op.Name, err)
		}
	}(op)
	return op, nil
}
