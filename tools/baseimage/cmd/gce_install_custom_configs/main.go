// Copyright (C) 2026 The Android Open Source Project
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
	"path/filepath"

	"github.com/google/android-cuttlefish/tools/baseimage/pkg/gce"
)

const (
	cuttlefishHODefaultsPath          = "/etc/default/cuttlefish-host_orchestrator"
	cuttlefishIntegrationDefaultsPath = "/etc/defaults/cuttlefish-integration"
)

// Flags
var (
	project                          string
	zone                             string
	sourceImageProject               string
	sourceImage                      string
	imageName                        string
	cuttlefishHODefaultsSrc          string
	cuttlefishIntegrationDefaultsSrc string
)

func init() {
	flag.StringVar(&project, "project", "", "GCP project whose resources will be used for creating the amended image")
	flag.StringVar(&zone, "zone", "us-central1-a", "GCP zone used for creating relevant resources")
	flag.StringVar(&sourceImageProject, "source-image-project", "", "Source image GCP project")
	flag.StringVar(&sourceImage, "source-image", "", "Source image name")
	flag.StringVar(&imageName, "image-name", "", "output GCE image name")
	flag.StringVar(&cuttlefishHODefaultsSrc, "cuttlefish-ho-defaults-src", "", "Local path to custom HO defaults")
	flag.StringVar(&cuttlefishIntegrationDefaultsSrc, "cuttlefish-integration-defaults-src", "", "Local path to cuttlefish integration defaults")
}

func uploadConfig(project, zone, insName, src, dst string) error {
	tmp := filepath.Join("/tmp", filepath.Base(src))
	if err := gce.UploadFile(project, zone, insName, src, tmp); err != nil {
		return err
	}
	cmd := fmt.Sprintf("sudo cp %s %s", tmp, filepath.Join(gce.BuildImageMountPoint, dst))
	if err := gce.RunCmd(project, zone, insName, cmd); err != nil {
		return err
	}
	return nil
}

func usage(msg string) string {
	errmsg := ""
	if msg != "" {
		errmsg = "error: " + msg + "\n\n"
	}
	return errmsg + `usage: gce_install_custom_configs -project PROJECT \
  -zone ZONE \
  -source-image-project SRC_IMG_PROJECT \
  -source-image SRC_IMG \
  -image-name IMG_NAME \
  [ -cuttlefish-ho-defaults-src SRC ] \
  [ -cuttlefish-integration-defaults-src SRC ] \
`
}

func main() {

	flag.Parse()

	if project == "" {
		log.Fatal(usage("`-project` must not be empty"))
	}
	if zone == "" {
		log.Fatal(usage("`-zone` must not be empty"))
	}
	if sourceImageProject == "" {
		log.Fatal(usage("`-source-image-project` must not be empty"))
	}
	if sourceImage == "" {
		log.Fatal(usage("`-source-image` must not be empty"))
	}
	if imageName == "" {
		log.Fatal(usage("`-image-name` must not be empty"))
	}
	if cuttlefishHODefaultsSrc == "" && cuttlefishIntegrationDefaultsSrc == "" {
		log.Fatal(usage("one or more custom configurations are required"))
	}

	buildImageOpts := gce.BuildImageOpts{
		Arch:               gce.ArchX86,
		SourceImageProject: sourceImageProject,
		SourceImage:        sourceImage,
		ImageName:          imageName,
		ModifyFunc: func(project, zone, insName string) error {
			if cuttlefishHODefaultsSrc != "" {
				if err := uploadConfig(project, zone, insName, cuttlefishHODefaultsSrc, cuttlefishHODefaultsPath); err != nil {
					return err
				}
			}

			if cuttlefishIntegrationDefaultsSrc != "" {
				if err := uploadConfig(project, zone, insName, cuttlefishIntegrationDefaultsSrc, cuttlefishIntegrationDefaultsPath); err != nil {
					return err
				}
			}

			return nil
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
