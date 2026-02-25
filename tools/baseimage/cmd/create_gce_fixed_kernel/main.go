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
	"maps"
	"slices"

	"github.com/google/android-cuttlefish/tools/baseimage/pkg/cli"
	"github.com/google/android-cuttlefish/tools/baseimage/pkg/gce"
	"github.com/google/android-cuttlefish/tools/baseimage/pkg/gce/scripts"
)

// Cuttlefish base images are based on debian images.
const debianSourceImageProject = "debian-cloud"

var sourceImageMap = map[gce.Arch]map[int]string{
	gce.ArchX86: {
		12: "debian-12-bookworm-v20260114",
		13: "debian-13-trixie-v20260114",
	},
	gce.ArchArm: {
		12: "debian-12-bookworm-arm64-v20260114",
		13: "debian-13-trixie-arm64-v20260114",
	},
}

// Flags
var (
	project       string
	zone          string
	arch          cli.Arch
	debianVersion int
	linuxImageDeb string
	imageName     string
)

func init() {
	flag.StringVar(&project, "project", "", "GCP project whose resources will be used for creating the amended image")
	flag.StringVar(&zone, "zone", "us-central1-a", "GCP zone used for creating relevant resources")
	flag.Var(&arch, "arch", "architecture of GCE image. Supports either x86_64 or arm64")
	flag.IntVar(&debianVersion, "debian-version", 13, "Debian version: https://www.debian.org/releases")
	flag.StringVar(&linuxImageDeb, "linux-image-deb", "", "linux-image-* package name. E.g. linux-image-6.1.0-40-cloud-amd64")
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
	if linuxImageDeb == "" {
		log.Fatal("usage: `-linux-image-deb` must not be empty")
	}
	if imageName == "" {
		log.Fatal("usage: `-image-name` must not be empty")
	}

	if _, ok := sourceImageMap[arch.GceArch()]; !ok {
		log.Fatalf("no source image found for arch %s: supported archs: %v",
			arch,
			slices.Collect(maps.Keys(sourceImageMap)))
	}

	if _, ok := sourceImageMap[arch.GceArch()][debianVersion]; !ok {
		log.Fatalf("no source image found for debian %d: supported versions: %v",
			debianVersion,
			slices.Collect(maps.Keys(sourceImageMap[arch.GceArch()])))
	}

	buildImageOpts := gce.BuildImageOpts{
		Arch:               arch.GceArch(),
		SourceImageProject: debianSourceImageProject,
		SourceImage:        sourceImageMap[arch.GceArch()][debianVersion],
		ImageName:          imageName,
		ModifyFunc: func(project, zone, insName string) error {
			if err := gce.UploadBashScript(project, zone, insName, "install_kernel_main.sh", scripts.InstallKernelMain); err != nil {
				return fmt.Errorf("error uploading script: %v", err)
			}
			return gce.RunCmd(project, zone, insName, "./install_kernel_main.sh "+linuxImageDeb)
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
