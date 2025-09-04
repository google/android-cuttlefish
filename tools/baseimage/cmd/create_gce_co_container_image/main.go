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
	"flag"
	"fmt"
	"log"
	"time"

	"github.com/google/android-cuttlefish/tools/baseimage/pkg/gce"
	"github.com/google/android-cuttlefish/tools/baseimage/pkg/gce/scripts"
	"github.com/google/uuid"
)

const (
	outImageName = "cuttlefish-cloud-image"
)

var (
	project      = flag.String("project", "", "GCE project whose resources will be used for creating the image")
	zone         = flag.String("zone", "us-central1-a", "GCE zone used for creating relevant resources")
	imageProject = flag.String("image_project", "debian-cloud", "GCE project managing target source image")
	imageFamily  = flag.String("image_family", "debian-12", "GCE image family of target source image")
	machineType  = flag.String("machine_type", "n1-standard-1", "GCE machine type used for creating image")
)

func rebootInstance(project, zone, instance string) error {
	if err := gce.RunCmd(project, zone, instance, "sudo reboot"); err != nil {
		return fmt.Errorf("failed to reboot: %v", err)
	}
	time.Sleep(2 * time.Minute)
	if err := gce.WaitForInstance(project, zone, instance); err != nil {
		return fmt.Errorf("error while waiting for instance: %v", err)
	}
	return nil
}

func uploadAndRunBashScript(project, zone, instance, script string) error {
	filename := fmt.Sprintf("image-build-script-%s.sh", uuid.NewString())

	if err := gce.UploadBashScript(project, zone, instance, filename, script); err != nil {
		return fmt.Errorf("failed to upload %q: %v", filename, err)
	}
	if err := gce.RunCmd(project, zone, instance, fmt.Sprintf("./%s", filename)); err != nil {
		return fmt.Errorf("failed to run %q: %v", filename, err)
	}
	if err := gce.RunCmd(project, zone, instance, fmt.Sprintf("rm %s", filename)); err != nil {
		return fmt.Errorf("failed to cleanup %q: %v", filename, err)
	}
	return nil
}

func createImageMain(project, zone, imageProject, imageFamily, machineType string) error {
	h, err := gce.NewGceHelper(project, zone)
	if err != nil {
		return fmt.Errorf("failed to create GCE helper: %v", err)
	}
	instance := fmt.Sprintf("cf-%s", uuid.NewString())

	log.Print("Initializing GCE image builder instance...")
	if _, err := h.CreateInstanceWithImageFamily(imageProject, imageFamily, machineType, instance); err != nil {
		return fmt.Errorf("failed to create instance: %v", err)
	}
	defer func() {
		if err := h.DeleteInstance(instance); err != nil {
			log.Printf("failed to delete instance: %v", err)
		}
	}()
	if err := gce.WaitForInstance(project, zone, instance); err != nil {
		return fmt.Errorf("error while waiting for instance: %v", err)
	}

	log.Print("Running scripts on GCE image builder instance...")
	if err := uploadAndRunBashScript(project, zone, instance, scripts.UpdateBackportKernel); err != nil {
		return err
	}
	if err := rebootInstance(project, zone, instance); err != nil {
		return err
	}
	if err := uploadAndRunBashScript(project, zone, instance, scripts.RemoveOldKernel); err != nil {
		return err
	}
	if err := uploadAndRunBashScript(project, zone, instance, scripts.InstallCODockerContainer); err != nil {
		return err
	}
	if err := rebootInstance(project, zone, instance); err != nil {
		return err
	}

	log.Print("Creating GCE image from GCE image builder instance...")
	if err := h.StopInstance(instance); err != nil {
		return fmt.Errorf("error stopping instance: %v", err)
	}
	if err := h.CreateImage(instance, outImageName, []string{"IDPF"}); err != nil {
		return fmt.Errorf("failed to create image: %v", err)
	}
	log.Printf("image %q was created successfully", outImageName)
	return nil
}

func main() {
	flag.Parse()
	if *project == "" {
		log.Fatal("usage: `--project` must not be empty")
	}
	if *zone == "" {
		log.Fatal("usage: `--zone` must not be empty")
	}
	if *imageProject == "" {
		log.Fatal("usage: `--image_project` must not be empty")
	}
	if *imageFamily == "" {
		log.Fatal("usage: `--image_family` must not be empty")
	}
	if *machineType == "" {
		log.Fatal("usage: `--machine_type` must not be empty")
	}

	if err := createImageMain(*project, *zone, *imageProject, *imageFamily, *machineType); err != nil {
		log.Fatal(err)
	}
}
