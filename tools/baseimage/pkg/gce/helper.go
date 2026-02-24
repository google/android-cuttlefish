// Copyright (C) 2025 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//	http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package gce

import (
	"context"
	"errors"
	"fmt"
	"log"
	"os/exec"
	"strings"
	"time"

	"google.golang.org/api/compute/v1"
)

type GceHelper struct {
	Service *compute.Service
	Project string
	Zone    string
}

func NewGceHelper(project, zone string) (*GceHelper, error) {
	service, err := compute.NewService(context.TODO())
	if err != nil {
		return nil, fmt.Errorf("failed to create service: %w", err)
	}
	return &GceHelper{
		Service: service,
		Project: project,
		Zone:    zone,
	}, nil
}

type CreateDiskOpts struct {
	SizeGb int64
}

func (h *GceHelper) CreateDisk(sourceImageProject, sourceImage, name string, opts CreateDiskOpts) (*compute.Disk, error) {
	payload := &compute.Disk{
		Name:        name,
		SourceImage: fmt.Sprintf("projects/%s/global/images/%s", sourceImageProject, sourceImage),
		SizeGb:      opts.SizeGb,
	}
	op, err := h.Service.Disks.Insert(h.Project, h.Zone, payload).Do()
	if err != nil {
		return nil, err
	}
	if err := h.waitForOperation(op); err != nil {
		return nil, err
	}
	return payload, nil
}

func (h *GceHelper) DeleteDisk(name string) error {
	op, err := h.Service.Disks.Delete(h.Project, h.Zone, name).Do()
	if err != nil {
		return err
	}
	return h.waitForOperation(op)
}

func (h *GceHelper) AttachDisk(ins, disk string) error {
	attachedDisk := &compute.AttachedDisk{
		Source: fmt.Sprintf("projects/%s/zones/%s/disks/%s", h.Project, h.Zone, disk),
	}
	op, err := h.Service.Instances.AttachDisk(h.Project, h.Zone, ins, attachedDisk).Do()
	if err != nil {
		return err
	}
	return h.waitForOperation(op)
}

func (h *GceHelper) DetachDisk(ins, disk string) error {
	op, err := h.Service.Instances.DetachDisk(h.Project, h.Zone, ins, disk).Do()
	if err != nil {
		return err
	}
	return h.waitForOperation(op)
}

func (h *GceHelper) CreateInstance(name string, arch Arch) (*compute.Instance, error) {
	var machineType, sourceImage string
	switch arch {
	case ArchX86:
		machineType = "n1-standard-16"
		sourceImage = "debian-12-bookworm-v20250415"
	case ArchArm:
		machineType = "t2a-standard-16"
		sourceImage = "debian-12-bookworm-arm64-v20250415"
	default:
		return nil, errors.New("unsupported arch")
	}
	payload := &compute.Instance{
		Name:        name,
		MachineType: fmt.Sprintf("zones/%s/machineTypes/%s", h.Zone, machineType),
		Disks: []*compute.AttachedDisk{
			{
				InitializeParams: &compute.AttachedDiskInitializeParams{
					SourceImage: fmt.Sprintf("projects/debian-cloud/global/images/%s", sourceImage),
				},
				Boot:       true,
				AutoDelete: true,
			},
		},
		NetworkInterfaces: []*compute.NetworkInterface{
			{
				AccessConfigs: []*compute.AccessConfig{
					{
						Name: "External NAT",
						Type: "ONE_TO_ONE_NAT",
					},
				},
			},
		},
	}
	op, err := h.Service.Instances.Insert(h.Project, h.Zone, payload).Do()
	if err != nil {
		return nil, err

	}
	if err := h.waitForOperation(op); err != nil {
		return nil, err
	}
	return payload, nil
}

func (h *GceHelper) CreateInstanceToValidateImage(name, imageProject, image string) (*compute.Instance, error) {
	payload := &compute.Instance{
		Name:           name,
		MachineType:    fmt.Sprintf("zones/%s/machineTypes/%s", h.Zone, "n1-standard-16"),
		MinCpuPlatform: "Intel Haswell",
		AdvancedMachineFeatures: &compute.AdvancedMachineFeatures{
			EnableNestedVirtualization: true,
		},
		Disks: []*compute.AttachedDisk{
			{
				InitializeParams: &compute.AttachedDiskInitializeParams{
					SourceImage: fmt.Sprintf("projects/%s/global/images/%s", imageProject, image),
				},
				Boot:       true,
				AutoDelete: true,
				DiskSizeGb: 200,
			},
		},
		NetworkInterfaces: []*compute.NetworkInterface{
			{
				AccessConfigs: []*compute.AccessConfig{
					{
						Name: "External NAT",
						Type: "ONE_TO_ONE_NAT",
					},
				},
			},
		},
		GuestAccelerators: []*compute.AcceleratorConfig{
			{
				AcceleratorCount: 1,
				AcceleratorType:  fmt.Sprintf("zones/%s/acceleratorTypes/nvidia-tesla-p100-vws", h.Zone),
			},
		},
		Scheduling: &compute.Scheduling{
			OnHostMaintenance: "TERMINATE",
		},
	}
	op, err := h.Service.Instances.Insert(h.Project, h.Zone, payload).Do()
	if err != nil {
		return nil, err

	}
	if err := h.waitForOperation(op); err != nil {
		return nil, err
	}
	return payload, nil
}

func (h *GceHelper) StopInstance(name string) error {
	op, err := h.Service.Instances.Stop(h.Project, h.Zone, name).Do()
	if err != nil {
		return err
	}
	return h.waitForOperation(op)
}

func (h *GceHelper) DeleteInstance(name string) error {
	op, err := h.Service.Instances.Delete(h.Project, h.Zone, name).Do()
	if err != nil {
		return err
	}
	return h.waitForOperation(op)
}

func (h *GceHelper) CreateImage(ins, disk, name string) error {
	payload := &compute.Image{
		Name:       name,
		SourceDisk: fmt.Sprintf("projects/%s/zones/%s/disks/%s", h.Project, h.Zone, disk),
		Licenses:   []string{"https://www.googleapis.com/compute/v1/projects/vm-options/global/licenses/enable-vmx"},
	}
	op, err := h.Service.Images.Insert(h.Project, payload).Do()
	if err != nil {
		return err
	}
	return h.waitForGlobalOperation(op)
}

func (h *GceHelper) waitForOperation(op *compute.Operation) error {
	for attempt := 0; attempt < 3 && op.Status != "DONE"; attempt++ {
		var err error
		op, err = h.Service.ZoneOperations.Wait(h.Project, h.Zone, op.Name).Do()
		if err != nil {
			return err
		}
		time.Sleep(30 * time.Second)
	}
	if op.Status != "DONE" {
		return fmt.Errorf("wait for operation %q: timed out", op.Name)
	}
	return nil
}

func (h *GceHelper) waitForGlobalOperation(op *compute.Operation) error {
	for attempt := 0; attempt < 3 && op.Status != "DONE"; attempt++ {
		var err error
		op, err = h.Service.GlobalOperations.Wait(h.Project, op.Name).Do()
		if err != nil {
			return err
		}
		time.Sleep(30 * time.Second)
	}
	if op.Status != "DONE" {
		return fmt.Errorf("wait for operation %q: timed out", op.Name)
	}
	return nil
}

func WaitForInstance(project, zone, ins string) error {
	for attempt := 0; attempt < 10; attempt++ {
		time.Sleep(30 * time.Second)
		if err := RunCmdWithOpts(project, zone, ins, "uptime", RunCmdOpts{PrintOutput: false}); err == nil {
			return nil
		}
	}
	return errors.New("waiting for instance timed out")
}

func UploadBashScript(project, zone, ins, scriptName, scriptContent string) error {
	r := strings.NewReplacer("\"", "\\\"", "$", "\\$")
	escapedContent := r.Replace(scriptContent)

	commands := []string{
		fmt.Sprintf("/usr/bin/echo \"%s\" > %s", escapedContent, scriptName),
		fmt.Sprintf("/usr/bin/cat %s", scriptName),
		fmt.Sprintf("/usr/bin/chmod +x %s", scriptName),
	}
	for _, c := range commands {
		if err := RunCmd(project, zone, ins, c); err != nil {
			return err
		}
	}
	return nil
}

func UploadFile(project, zone, ins, src string, dst string) error {
	return runCmd(RunCmdOpts{PrintOutput: true}, "gcloud", "compute", "scp", "--project", project, "--zone", zone, src, ins+":"+dst)
}

func RunCmd(project, zone, ins, cmd string) error {
	return runCmd(RunCmdOpts{PrintOutput: true}, "gcloud", "compute", "ssh", "--project", project, "--zone", zone, ins, "--command", cmd)
}

type RunCmdOpts struct {
	PrintOutput bool
}

func RunCmdWithOpts(project, zone, ins, cmd string, opts RunCmdOpts) error {
	return runCmd(opts, "gcloud", "compute", "ssh", "--project", project, "--zone", zone, ins, "--command", cmd)
}

func runCmd(opts RunCmdOpts, name string, args ...string) error {
	cmd := exec.CommandContext(context.TODO(), name, args...)
	if opts.PrintOutput {
		cmd.Stdout = log.Writer()
		cmd.Stderr = log.Writer()
	}
	log.Printf("Executing command: `%s`\n", cmd.String())
	return cmd.Run()
}
