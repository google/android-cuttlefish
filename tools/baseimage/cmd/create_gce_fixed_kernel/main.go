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
	"maps"
	"regexp"
	"slices"
	"strings"

	"github.com/google/android-cuttlefish/tools/baseimage/pkg/cli"
	"github.com/google/android-cuttlefish/tools/baseimage/pkg/gce"
	"github.com/google/android-cuttlefish/tools/baseimage/pkg/gce/scripts"
	"github.com/google/android-cuttlefish/tools/baseimage/pkg/nvidia"
)

// Cuttlefish base images are based on debian images.
const debianSourceImageProject = "debian-cloud"

var linuxImageDebRe = regexp.MustCompile(`^linux-image-[0-9]+\.[0-9]+\.[0-9]+[a-zA-Z0-9.+~-]*$`)

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
	nvidiaFlags   cli.NvidiaFlags
)

func registerFlags(fs *flag.FlagSet) {
	fs.StringVar(&project, "project", "", "GCP project whose resources will be used for creating the amended image")
	fs.StringVar(&zone, "zone", "us-central1-a", "GCP zone used for creating relevant resources")
	fs.Var(&arch, "arch", "architecture of GCE image. Supports either x86_64 or arm64")
	fs.IntVar(&debianVersion, "debian-version", 13, "Debian version: https://www.debian.org/releases")
	fs.StringVar(&linuxImageDeb, "linux-image-deb", "", "linux-image-* package name. E.g. linux-image-6.1.0-40-cloud-amd64. Defaults to the newest kernel, backports included, that the resolved NVIDIA driver builds against")
	fs.StringVar(&imageName, "image-name", "", "output GCE image name")
	nvidiaFlags.Register(fs)
}

func init() {
	registerFlags(flag.CommandLine)
}

func validateFlags(fs *flag.FlagSet) error {
	if arch.String() == "" {
		return errors.New("usage: `-arch` must not be empty")
	}
	if project == "" {
		return errors.New("usage: `-project` must not be empty")
	}
	if zone == "" {
		return errors.New("usage: `-zone` must not be empty")
	}
	if imageName == "" {
		return errors.New("usage: `-image-name` must not be empty")
	}
	if _, ok := sourceImageMap[arch.GceArch()]; !ok {
		return fmt.Errorf("no source image found for arch %s: supported archs: %v",
			arch, slices.Collect(maps.Keys(sourceImageMap)))
	}
	if _, ok := sourceImageMap[arch.GceArch()][debianVersion]; !ok {
		return fmt.Errorf("no source image found for debian %d: supported versions: %v",
			debianVersion, slices.Collect(maps.Keys(sourceImageMap[arch.GceArch()])))
	}
	if linuxImageDeb != "" {
		if err := validateLinuxImageDeb(linuxImageDeb, arch.GceArch()); err != nil {
			return err
		}
		if nvidiaFlags.AnyExplicit(fs) {
			return errors.New("`-nvidia-*` flags cannot be combined with `-linux-image-deb` in create_gce_fixed_kernel; pass them to create_gce_base_image instead")
		}
	}
	return nil
}

func validateLinuxImageDeb(pkg string, targetArch gce.Arch) error {
	if !linuxImageDebRe.MatchString(pkg) {
		return fmt.Errorf("invalid `-linux-image-deb` %q: expected a concrete kernel package like linux-image-6.12.118+deb13-cloud-amd64", pkg)
	}
	switch targetArch {
	case gce.ArchX86:
		if strings.HasSuffix(pkg, "-arm64") {
			return fmt.Errorf("`-linux-image-deb` %q targets arm64, which conflicts with `-arch=x86_64`", pkg)
		}
	case gce.ArchArm:
		if strings.HasSuffix(pkg, "-amd64") {
			return fmt.Errorf("`-linux-image-deb` %q targets amd64, which conflicts with `-arch=arm64`", pkg)
		}
	}
	return nil
}

func shellQuote(s string) string {
	return "'" + strings.ReplaceAll(s, "'", "'\"'\"'") + "'"
}

func main() {
	flag.Parse()
	if err := validateFlags(flag.CommandLine); err != nil {
		log.Fatal(err)
	}

	var resolvedDriver *nvidia.Driver
	if linuxImageDeb == "" {
		nvidiaOpts, err := nvidiaFlags.ToResolveOpts(flag.CommandLine, arch.GceArch())
		if err != nil {
			log.Fatal(err)
		}
		resolvedDriver, err = nvidia.NewClient().Resolve(context.Background(), nvidiaOpts)
		if err != nil {
			log.Fatalf("resolving NVIDIA driver: %v", err)
		}
		log.Printf("resolved NVIDIA driver: version=%s branch=%s",
			resolvedDriver.Version, resolvedDriver.Branch)
	}

	h, err := gce.NewGceHelper(project, zone)
	if err != nil {
		log.Fatal(err)
	}

	sourceImage := sourceImageMap[arch.GceArch()][debianVersion]
	if srcExists, err := h.ImageExists(debianSourceImageProject, sourceImage); err != nil || !srcExists {
		if err != nil {
			log.Fatal(err)
		}
		log.Fatalf("source image %q does not exist in project %q", sourceImage, debianSourceImageProject)
	}

	if exists, err := h.ImageExists(project, imageName); err != nil || exists {
		if err != nil {
			log.Fatal(err)
		}
		log.Fatalf("image %q already exists in project %q", imageName, project)
	}

	buildImageOpts := gce.BuildImageOpts{
		Arch:               arch.GceArch(),
		SourceImageProject: debianSourceImageProject,
		SourceImage:        sourceImage,
		ImageName:          imageName,
		Labels:             cli.NvidiaLabels(resolvedDriver),
		ModifyFunc: func(project, zone, insName string) error {
			var args []string
			if linuxImageDeb == "" {
				if err := gce.UploadBashScript(project, zone, insName, "install_nvidia.sh", scripts.InstallNvidia); err != nil {
					return fmt.Errorf("error uploading script: %v", err)
				}
				if err := gce.UploadBashScript(project, zone, insName, "select_default_kernel.sh", scripts.SelectDefaultKernel); err != nil {
					return fmt.Errorf("error uploading script: %v", err)
				}
				args = []string{
					"--nvidia-version", shellQuote(resolvedDriver.Version),
					"--runfile-url", shellQuote(resolvedDriver.RunfileURL),
				}
			} else {
				args = []string{"--linux-image-deb", shellQuote(linuxImageDeb)}
			}
			if err := gce.UploadBashScript(project, zone, insName, "install_kernel_main.sh", scripts.InstallKernelMain); err != nil {
				return fmt.Errorf("error uploading script: %v", err)
			}
			return gce.RunCmd(project, zone, insName, "./install_kernel_main.sh "+strings.Join(args, " "))
		},
	}

	if err := h.BuildImage(project, zone, buildImageOpts); err != nil {
		log.Fatal(err)
	}
}
