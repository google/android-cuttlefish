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
	"os/user"

	"github.com/google/android-cuttlefish/tools/baseimage/pkg/gce"
	"github.com/google/android-cuttlefish/tools/baseimage/pkg/gce/scripts"
)

var (
	project      = flag.String("project", "", "GCE project whose resources will be used for creating the image")
	zone         = flag.String("zone", "us-west1-b", "GCE zone used for creating relevant resources")
	imageProject = flag.String("image-project", "", "Image GCP project")
	image        = flag.String("image", "", "Image name")
)

func username() (string, error) {
	u, err := user.Current()
	if err != nil {
		return "", nil
	}
	return u.Username, nil
}

func validateImageMain(project, zone, imageProject, image string) error {
	h, err := gce.NewGceHelper(project, zone)
	if err != nil {
		return fmt.Errorf("failed to create GCE helper: %w", err)
	}
	whoami, err := username()
	if err != nil {
		return fmt.Errorf("error getting current user: %w", err)
	}
	insName := fmt.Sprintf("%s-validate-image", whoami)
	defer func() {
		log.Printf("cleanup: deleting instance %q...", insName)
		if err := h.DeleteInstance(insName); err != nil {
			log.Printf("cleanup: error deleting instance: %v", err)
		} else {
			log.Println("cleanup: instance deleted")
		}
	}()
	log.Println("creating instance...")
	_, err = h.CreateInstanceToValidateImage(insName, imageProject, image)
	if err != nil {
		return fmt.Errorf("failed to create instance: %w", err)
	}
	log.Printf("instance created: %q", insName)
	if err := gce.WaitForInstance(project, zone, insName); err != nil {
		return fmt.Errorf("waiting for instance error: %v", err)
	}
	if err := gce.UploadBashScript(project, zone, insName, "validate_cuttlefish_image.sh", scripts.ValidateCuttlefishImage); err != nil {
		return fmt.Errorf("error uploading script: %v", err)
	}
	if err := gce.RunCmd(project, zone, insName, "./validate_cuttlefish_image.sh"); err != nil {
		return err
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
	if *imageProject == "" {
		log.Fatal("usage: `-image-project` must not be empty")
	}
	if *image == "" {
		log.Fatal("usage: `-image` must not be empty")
	}

	if err := validateImageMain(*project, *zone, *imageProject, *image); err != nil {
		log.Fatal(err)
	}
}
