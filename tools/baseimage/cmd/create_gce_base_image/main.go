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
	"strings"

	"github.com/google/android-cuttlefish/tools/baseimage/pkg/cli"
	"github.com/google/android-cuttlefish/tools/baseimage/pkg/gce"
	"github.com/google/android-cuttlefish/tools/baseimage/pkg/gce/scripts"
	"github.com/google/android-cuttlefish/tools/baseimage/pkg/nvidia"
)

// Flags
var (
	project            string
	zone               string
	arch               cli.Arch
	sourceImageProject string
	sourceImage        string
	imageName          string
	nvidiaFlags        cli.NvidiaFlags
)

func init() {
	flag.StringVar(&project, "project", "", "GCP project whose resources will be used for creating the amended image")
	flag.StringVar(&zone, "zone", "us-central1-a", "GCP zone used for creating relevant resources")
	flag.Var(&arch, "arch", "architecture of GCE image. Supports either x86_64 or arm64")
	flag.StringVar(&sourceImageProject, "source-image-project", "", "Source image GCP project")
	flag.StringVar(&sourceImage, "source-image", "", "Source image name")
	flag.StringVar(&imageName, "image-name", "", "output GCE image name")
	nvidiaFlags.Register(flag.CommandLine)
}

func validateFlags() error {
	if arch.String() == "" {
		return errors.New("usage: `-arch` must not be empty")
	}
	if project == "" {
		return errors.New("usage: `-project` must not be empty")
	}
	if zone == "" {
		return errors.New("usage: `-zone` must not be empty")
	}
	if sourceImageProject == "" {
		return errors.New("usage: `-source-image-project` must not be empty")
	}
	if sourceImage == "" {
		return errors.New("usage: `-source-image` must not be empty")
	}
	if imageName == "" {
		return errors.New("usage: `-image-name` must not be empty")
	}
	return nil
}

func shellQuote(s string) string {
	return "'" + strings.ReplaceAll(s, "'", "'\"'\"'") + "'"
}

func main() {
	flag.Parse()
	if err := validateFlags(); err != nil {
		log.Fatal(err)
	}

	targetArch := arch.GceArch()
	nvidiaOpts, err := nvidiaFlags.ToResolveOpts(flag.CommandLine, targetArch)
	if err != nil {
		log.Fatal(err)
	}

	h, err := gce.NewGceHelper(project, zone)
	if err != nil {
		log.Fatal(err)
	}

	srcImg, err := h.Image(sourceImageProject, sourceImage)
	if err != nil {
		log.Fatalf("looking up source image %q in project %q: %v", sourceImage, sourceImageProject, err)
	}
	if !gce.ImageMatchesArch(targetArch, srcImg) {
		log.Fatalf("source image %q has architecture %q, which does not match `-arch=%s`",
			srcImg.Name, srcImg.Architecture, arch)
	}

	if exists, err := h.ImageExists(project, imageName); err != nil || exists {
		if err != nil {
			log.Fatal(err)
		}
		log.Fatalf("image %q already exists in project %q", imageName, project)
	}

	// Reuse the NVIDIA driver validated by create_gce_fixed_kernel when the
	// caller did not pass any explicit -nvidia-* override flags.
	if !nvidiaFlags.AnyExplicit(flag.CommandLine) {
		if labelVer, ok := cli.NvidiaVersionFromLabels(srcImg.Labels); ok {
			log.Printf("inheriting validated NVIDIA driver from source image label: version=%s", labelVer)
			nvidiaOpts.Version = labelVer
			nvidiaOpts.BranchExplicit = false
		}
	}

	resolvedDriver, err := nvidia.NewClient().Resolve(context.Background(), nvidiaOpts)
	if err != nil {
		log.Fatalf("resolving NVIDIA driver: %v", err)
	}
	log.Printf("resolved NVIDIA driver: version=%s branch=%s",
		resolvedDriver.Version, resolvedDriver.Branch)

	buildImageOpts := gce.BuildImageOpts{
		Arch:               targetArch,
		SourceImageProject: sourceImageProject,
		SourceImage:        srcImg.Name,
		ImageName:          imageName,
		Labels:             cli.NvidiaLabels(resolvedDriver),
		ModifyFunc: func(project, zone, insName string) error {
			if err := gce.UploadBashScript(project, zone, insName, "install_nvidia.sh", scripts.InstallNvidia); err != nil {
				return fmt.Errorf("error uploading script: %v", err)
			}
			if err := gce.UploadBashScript(project, zone, insName, "create_base_image_main.sh", scripts.CreateBaseImageMain); err != nil {
				return fmt.Errorf("error uploading script: %v", err)
			}
			args := []string{
				"--nvidia-version", shellQuote(resolvedDriver.Version),
				"--runfile-url", shellQuote(resolvedDriver.RunfileURL),
			}
			return gce.RunCmd(project, zone, insName, "./create_base_image_main.sh "+strings.Join(args, " "))
		},
	}

	if err := h.BuildImage(project, zone, buildImageOpts); err != nil {
		log.Fatal(err)
	}
}
