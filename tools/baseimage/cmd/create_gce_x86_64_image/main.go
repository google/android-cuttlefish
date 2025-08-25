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

package main

import (
	"context"
	"errors"
	"flag"
	"fmt"
	"log"
	"os/exec"
	"strings"
	"time"

	"github.com/google/android-cuttlefish/tools/baseimage/pkg/gce/scripts"
	"google.golang.org/api/compute/v1"
)

// Cuttlefish base images are based on debian images.
const (
	debianSourceImageProject = "debian-cloud"
	debianSourceImage        = "debian-12-bookworm-v20250610"
)

const (
	outImageName = "cuttlefish-base-image"
)

var (
	project = flag.String("project", "", "GCE project whose resources will be used for creating the image")
	zone    = flag.String("zone", "us-central1-a", "GCE zone used for creating relevant resources")
)

type gceHelper struct {
	Service *compute.Service
	Project string
	Zone    string
}

func (h *gceHelper) waitForOperation(op *compute.Operation) error {
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

func (h *gceHelper) waitForGlobalOperation(op *compute.Operation) error {
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

func (h *gceHelper) CreateDisk(sourceImageProject, sourceImage, name string) (*compute.Disk, error) {
	payload := &compute.Disk{
		Name:        name,
		SourceImage: fmt.Sprintf("projects/%s/global/images/%s", sourceImageProject, sourceImage),
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

func (h *gceHelper) DeleteDisk(name string) error {
	op, err := h.Service.Disks.Delete(h.Project, h.Zone, name).Do()
	if err != nil {
		return err
	}
	return h.waitForOperation(op)
}

func (h *gceHelper) CreateInstance(name string) (*compute.Instance, error) {
	payload := &compute.Instance{
		Name:           name,
		MachineType:    fmt.Sprintf("zones/%s/machineTypes/%s", h.Zone, "n1-standard-16"),
		MinCpuPlatform: "Intel Haswell",
		Disks: []*compute.AttachedDisk{
			{
				InitializeParams: &compute.AttachedDiskInitializeParams{
					SourceImage: "projects/debian-cloud/global/images/debian-12-bookworm-v20250415",
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

func (h *gceHelper) DeleteInstance(name string) error {
	op, err := h.Service.Instances.Delete(h.Project, h.Zone, name).Do()
	if err != nil {
		return err
	}
	return h.waitForOperation(op)
}

func (h *gceHelper) AttachDisk(ins, disk string) error {
	attachedDisk := &compute.AttachedDisk{
		Source: fmt.Sprintf("projects/%s/zones/%s/disks/%s", h.Project, h.Zone, disk),
	}
	op, err := h.Service.Instances.AttachDisk(h.Project, h.Zone, ins, attachedDisk).Do()
	if err != nil {
		return err
	}
	return h.waitForOperation(op)
}

func (h *gceHelper) DetachDisk(ins, disk string) error {
	op, err := h.Service.Instances.DetachDisk(h.Project, h.Zone, ins, disk).Do()
	if err != nil {
		return err
	}
	return h.waitForOperation(op)
}

func (h *gceHelper) CreateImage(ins, disk, name string) error {
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

func runCmd(name string, args ...string) error {
	cmd := exec.CommandContext(context.TODO(), name, args...)
	cmd.Stdout = log.Writer()
	cmd.Stderr = log.Writer()
	log.Printf("Executing command: `%s`\n", cmd.String())
	return cmd.Run()
}

func waitForInstance(project, zone, name string) error {
	for attempt := 0; attempt < 3; attempt++ {
		time.Sleep(30 * time.Second)
		log.Printf("wait for instance: uptime attempt number: %d", attempt)
		if err := runCmd("gcloud", "compute", "ssh", "--project", project, "--zone", zone, name, "--command", "uptime"); err == nil {
			return nil
		}
	}
	return errors.New("waiting for instance timed out")
}

func uploadBashScript(project, zone, insName, scriptName, scriptContent string) error {
	r := strings.NewReplacer("\"", "\\\"", "$", "\\$")
	escapedContent := r.Replace(scriptContent)

	commands := []string{
		fmt.Sprintf("/usr/bin/echo \"%s\" > %s", escapedContent, scriptName),
		fmt.Sprintf("/usr/bin/cat %s", scriptName),
		fmt.Sprintf("/usr/bin/chmod +x %s", scriptName),
	}
	for _, c := range commands {
		if err := runCmd("gcloud", "compute", "ssh", "--project", project, "--zone", zone, insName, "--command", c); err != nil {
			return err
		}
	}
	return nil
}

func createImageMain(project, zone string) error {
	ctx := context.Background()
	service, err := compute.NewService(ctx)
	if err != nil {
		return fmt.Errorf("failed to create service: %w", err)
	}
	gce := &gceHelper{
		Service: service,
		Project: project,
		Zone:    zone,
	}
	insName := outImageName
	attachedDiskName := fmt.Sprintf("%s-attached-disk", insName)
	defer func() {
		// DetachDisk
		log.Printf("cleanup: detaching disk %q from instance %q...", attachedDiskName, insName)
		if err := gce.DetachDisk(insName, attachedDiskName); err != nil {
			log.Printf("cleanup: error detaching disk: %v", err)
		} else {
			log.Println("cleanup: disk detached")
		}
		// Delete Instance
		log.Printf("cleanup: deleting instance %q...", insName)
		if err := gce.DeleteInstance(insName); err != nil {
			log.Printf("cleanup: error deleting instance: %v", err)
		} else {
			log.Println("cleanup: instance deleted")
		}
		// Delete Disk
		log.Printf("cleanup: deleting disk %q...", attachedDiskName)
		if err := gce.DeleteDisk(attachedDiskName); err != nil {
			log.Printf("cleanup: error deleting disk: %v", err)
		} else {
			log.Println("cleanup: disk deleted")
		}
	}()
	log.Println("creating disk...")
	disk, err := gce.CreateDisk(debianSourceImageProject, debianSourceImage, attachedDiskName)
	if err != nil {
		return fmt.Errorf("failed to create disk: %w", err)
	}
	log.Printf("disk created: %q", attachedDiskName)
	log.Println("creating instance...")
	ins, err := gce.CreateInstance(insName)
	if err != nil {
		return fmt.Errorf("failed to create instance: %w", err)
	}
	log.Printf("instance created: %q", insName)
	log.Println("attaching disk...")
	if err := gce.AttachDisk(insName, attachedDiskName); err != nil {
		log.Fatalf("failed to attach disk %q to instance %q: %v", disk.Name, ins.Name, err)
	}
	log.Println("disk attached")

	if err := waitForInstance(project, zone, insName); err != nil {
		return fmt.Errorf("waiting for instance error: %v", err)
	}
	// Upload Scripts
	if err := uploadBashScript(project, zone, insName, "update_kernel.sh", scripts.UpdateKernel); err != nil {
		return fmt.Errorf("error uploading update kernel script: %v", err)
	}
	if err := uploadBashScript(project, zone, insName, "remove_old_kernel.sh", scripts.RemoveOldKernel); err != nil {
		return fmt.Errorf("error uploading update kernel script: %v", err)
	}
	if err := uploadBashScript(project, zone, insName, "create_base_image.sh", scripts.CreateBaseImage); err != nil {
		return fmt.Errorf("error uploading update kernel script: %v", err)
	}
	if err := uploadBashScript(project, zone, insName, "install_nvidia.sh", scripts.InstallNvidia); err != nil {
		return fmt.Errorf("error uploading install nvidia script: %v", err)
	}
	// Execute Scripts
	if err := runCmd("gcloud", "compute", "ssh", "--project", project, "--zone", zone, insName, "--command", "./update_kernel.sh"); err != nil {
		return err
	}
	time.Sleep(2 * time.Minute) // update kernel script ends up rebooting the instance
	if err := waitForInstance(project, zone, insName); err != nil {
		return fmt.Errorf("waiting for instance error: %v", err)
	}
	if err := runCmd("gcloud", "compute", "ssh", "--project", project, "--zone", zone, insName, "--command", "./remove_old_kernel.sh"); err != nil {
		return err
	}
	if err := runCmd("gcloud", "compute", "ssh", "--project", project, "--zone", zone, insName, "--command", "./create_base_image.sh"); err != nil {
		return err
	}
	// Reboot the instance to force a clean umount of the attached disk's file system.
	if err := runCmd("gcloud", "compute", "ssh", "--project", project, "--zone", zone, insName, "--command", "sudo reboot"); err != nil {
		return err
	}
	time.Sleep(2 * time.Minute)
	if err := waitForInstance(project, zone, insName); err != nil {
		return fmt.Errorf("waiting for instance error: %v", err)
	}
	log.Printf("deleting instance %q...", insName)
	if err := gce.DeleteInstance(insName); err != nil {
		return fmt.Errorf("error deleting instance: %v", err)
	}
	if err := gce.CreateImage(insName, attachedDiskName, outImageName); err != nil {
		return fmt.Errorf("failed to create image: %w", err)
	}
	return nil
}

func main() {
	flag.Parse()

	if *project == "" {
		log.Fatal("usage: `-project` must not be empty")
	}
	if *zone == "" {
		log.Fatal("usage: `-zone` must not be empty")
	}

	if err := createImageMain(*project, *zone); err != nil {
		log.Fatal(err)
	}
	log.Printf("image %q was created successfully", outImageName)
	fmt.Printf(`Copy the image somewhere else:
gcloud compute images create \
  --source-image-project=%s \
  --source-image=%s \
  --project=[DEST_PROJECT] \
  --family=[DEST_IMAGE_FAMILY] [DEST_IMAGE_NAME]
`,
		*project,
		outImageName,
	)
}
