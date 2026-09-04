// Copyright 2026 Google LLC
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
	"os"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/cvd"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/exec"
	"github.com/google/android-cuttlefish/frontend/src/liboperator/operator"
)

type InjectInputDeviceEventsActionOpts struct {
	Selector         cvd.InstanceSelector
	DeviceName       string
	EventsFilePath   string
	OperationManager OperationManager
	ExecContext      exec.ExecContext
}

type InjectInputDeviceEventsAction struct {
	selector       cvd.InstanceSelector
	deviceName     string
	eventsFilePath string
	om             OperationManager
	cvdCLI         *cvd.CLI
}

func NewInjectInputDeviceEventsAction(opts InjectInputDeviceEventsActionOpts) *InjectInputDeviceEventsAction {
	return &InjectInputDeviceEventsAction{
		selector:       opts.Selector,
		deviceName:     opts.DeviceName,
		eventsFilePath: opts.EventsFilePath,
		om:             opts.OperationManager,
		cvdCLI:         cvd.NewCLI(opts.ExecContext),
	}
}

func (a *InjectInputDeviceEventsAction) Run() (apiv1.Operation, error) {
	if a.selector.GroupName == "" || a.selector.Name == "" {
		return apiv1.Operation{}, operator.NewBadRequestError("empty group or instance name", nil)
	}
	if a.deviceName == "" {
		return apiv1.Operation{}, operator.NewBadRequestError("empty device name", nil)
	}
	op := a.om.New()
	go func(op apiv1.Operation, filePath string) {
		defer os.Remove(filePath)
		result := &OperationResult{}
		result.Value = &apiv1.EmptyResponse{}
		if err := a.cvdCLI.LazySelectInstance(a.selector).InjectInputDeviceEvents(a.deviceName, filePath); err != nil {
			result.Error = operator.NewInternalError("failed to inject input device events", err)
		}
		if err := a.om.Complete(op.Name, result); err != nil {
			log.Printf("error completing operation %q: %v\n", op.Name, err)
		}
	}(op, a.eventsFilePath)
	return op, nil
}
