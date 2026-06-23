// Copyright 2022 Google LLC
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
	"os"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/cvd"
	hoexec "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/exec"
	"github.com/google/android-cuttlefish/frontend/src/liboperator/operator"
)

type Validator interface {
	Validate() error
}

type EmptyFieldError string

func (s EmptyFieldError) Error() string {
	return fmt.Sprintf("field %v is empty", string(s))
}

type IMPaths struct {
	CVDBugReportsDir    string
	SnapshotsRootDir    string
	ImageDirectoriesDir string
}

func CvdGroupToAPIObject(group *cvd.Group) []*apiv1.CVD {
	result := make([]*apiv1.CVD, len(group.Instances))
	for i, item := range group.Instances {
		result[i] = CvdInstanceToAPIObject(item, group.Name)
	}
	return result
}

func CvdInstanceToAPIObject(instance *cvd.Instance, group string) *apiv1.CVD {
	return &apiv1.CVD{
		Group:          group,
		Name:           instance.Name,
		Status:         instance.Status(),
		Displays:       instance.Displays(),
		WebRTCDeviceID: instance.WebRTCDeviceID(),
		ADBSerial:      instance.ADBSerial(),
		ADBPort:        instance.ADBPort(),
	}
}

func findGroup(groups []*cvd.Group, name string) (bool, *cvd.Group) {
	for _, e := range groups {
		if e.Name == name {
			return true, e
		}
	}
	return false, nil
}

func findInstance(group *cvd.Group, name string) (bool, *cvd.Instance) {
	for _, e := range group.Instances {
		if e.Name == name {
			return true, e
		}
	}
	return false, &cvd.Instance{}
}

func CVDLogsDir(ctx hoexec.ExecContext, groupName, name string) (string, error) {
	cvdCLI := cvd.NewCLI(ctx)
	fleet, err := cvdCLI.Fleet()
	if err != nil {
		return "", err
	}
	ok, group := findGroup(fleet, groupName)
	if !ok {
		return "", operator.NewNotFoundError(fmt.Sprintf("Group %q not found", groupName), nil)
	}
	ok, ins := findInstance(group, name)
	if !ok {
		return "", operator.NewNotFoundError(fmt.Sprintf("Instance %q not found", name), nil)
	}
	return ins.InstanceDir() + "/logs", nil
}

// Fails if the directory already exists.
func createNewDir(dir string) error {
	err := os.Mkdir(dir, 0774)
	if err != nil {
		return err
	}
	// Sets dir permission regardless of umask.
	return os.Chmod(dir, 0774)
}

func createDir(dir string) error {
	if err := createNewDir(dir); os.IsExist(err) {
		return nil
	} else {
		return err
	}
}

func fileExist(name string) (bool, error) {
	if _, err := os.Stat(name); err == nil {
		return true, nil
	} else if os.IsNotExist(err) {
		return false, nil
	} else {
		return false, err
	}
}

// Validates whether the current host is valid to run CVDs.
type HostValidator struct {
	ExecContext hoexec.ExecContext
}

func (v *HostValidator) Validate() error {
	if ok, _ := fileExist("/dev/kvm"); !ok {
		return operator.NewInternalError("Nested virtualization is not enabled.", nil)
	}
	return nil
}
