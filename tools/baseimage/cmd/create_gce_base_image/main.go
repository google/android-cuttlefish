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

	"github.com/google/android-cuttlefish/tools/baseimage/pkg/cli"
	"github.com/google/android-cuttlefish/tools/baseimage/pkg/gce"
	"github.com/google/android-cuttlefish/tools/baseimage/pkg/gce/scripts"
)

// Flags
var (
	project            string
	zone               string
	arch               cli.Arch
	sourceImageProject string
	sourceImage        string
	imageName          string
)

func init() {
	flag.StringVar(&project, "project", "", "GCP project whose resources will be used for creating the amended image")
	flag.StringVar(&zone, "zone", "us-central1-a", "GCP zone used for creating relevant resources")
	flag.Var(&arch, "arch", "architecture of GCE image. Supports either x86_64 or arm64")
	flag.StringVar(&sourceImageProject, "source-image-project", "", "Source image GCP project")
	flag.StringVar(&sourceImage, "source-image", "", "Source image name")
	flag.StringVar(&imageName, "image-name", "", "output GCE image name")
}

func main() {
	flag.Parse()

	if project == "" {
		log.Fatal("usage: `-project` must not be empty")
	}
	if zone == "" {
		log.Fatal("usage: `-zone` must not be empty")
	}
	if sourceImageProject == "" {
		log.Fatal("usage: `-source-image-project` must not be empty")
	}
	if sourceImage == "" {
		log.Fatal("usage: `-source-image` must not be empty")
	}
	if imageName == "" {
		log.Fatal("usage: `-image-name` must not be empty")
	}

	buildImageOpts := gce.BuildImageOpts{
		Arch:               arch.GceArch(),
		SourceImageProject: sourceImageProject,
		SourceImage:        sourceImage,
		ImageName:          imageName,
		ModifyFunc: func(project, zone, insName string) error {
			if err := gce.UploadBashScript(project, zone, insName, "install_nvidia.sh", scripts.InstallNvidia); err != nil {
				return fmt.Errorf("error uploading script: %v", err)
			}
			if err := gce.UploadBashScript(project, zone, insName, "create_base_image_main.sh", scripts.CreateBaseImageMain); err != nil {
				return fmt.Errorf("error uploading script: %v", err)
			}
			return gce.RunCmd(project, zone, insName, "./create_base_image_main.sh")
		},
	}

	h, err := gce.NewGceHelper(project, zone)
	if err != nil {
		log.Fatal(err)
	}
	if err := h.BuildImage(project, zone, buildImageOpts); err != nil {
		log.Fatal(err)
	}
}
