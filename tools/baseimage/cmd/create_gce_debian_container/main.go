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
	"google.golang.org/api/googleapi"
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
	imageName     string
)

func init() {
	flag.StringVar(&project, "project", "", "GCP project whose resources will be used for creating the amended image")
	flag.StringVar(&zone, "zone", "us-central1-a", "GCP zone used for creating relevant resources")
	flag.Var(&arch, "arch", "architecture of GCE image. Supports either x86_64 or arm64")
	flag.IntVar(&debianVersion, "debian-version", 13, "Debian version: https://www.debian.org/releases")
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
			if err := gce.UploadBashScript(project, zone, insName, "install_docker.sh", scripts.InstallDocker); err != nil {
				return fmt.Errorf("error uploading script: %v", err)
			}
			return gce.RunCmd(project, zone, insName, "./install_docker.sh")
		},
	}

	h, err := gce.NewGceHelper(project, zone)
	if err != nil {
		log.Fatal(err)
	}

	_, err = h.Service.Images.Get(project, imageName).Do()
	if err == nil {
		log.Fatalf("image %q already exists", imageName)
	}
	gerr, ok := err.(*googleapi.Error)
	if !ok || gerr.Code != 404 {
		log.Fatalf("failed to check if image exists: %v", err)
	}

	if err := h.BuildImage(project, zone, buildImageOpts); err != nil {
		log.Fatal(err)
	}
}
