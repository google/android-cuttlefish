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

	"github.com/google/android-cuttlefish/tools/baseimage/pkg/gce"
)

// Flags
var (
	project            string
	zone               string
	sourceImageProject string
	sourceImage        string
	destImageProject   string
	sizeGb             int64
	imageName          string
)

func init() {
	flag.StringVar(&project, "project", "", "GCP project whose resources will be used for creating the resized image")
	flag.StringVar(&zone, "zone", "us-central1-a", "GCP zone used for creating relevant resources")
	flag.StringVar(&sourceImageProject, "source-image-project", "", "Source image GCP project")
	flag.StringVar(&sourceImage, "source-image", "", "Source image name")
	flag.StringVar(&destImageProject, "dest-image-project", "", "Destination image GCP project (defaults to -project)")
	flag.Int64Var(&sizeGb, "size-gb", 160, "New image size in GB")
	flag.StringVar(&imageName, "image-name", "", "Output GCE image name (defaults to <source-image>--<size-gb>gb)")
}

func defaultOutputImageName(srcImage string, size int64) string {
	return fmt.Sprintf("%s--%dgb", srcImage, size)
}

func resizeImage(project, zone, sourceImageProject, sourceImage, destImageProject, imageName string, sizeGb int64) error {
	h, err := gce.NewGceHelper(project, zone)
	if err != nil {
		return err
	}

	srcImg, err := h.GetImage(sourceImageProject, sourceImage)
	if err != nil {
		return fmt.Errorf("failed to check if source image %q exists: %w", sourceImage, err)
	}
	if srcImg == nil {
		return fmt.Errorf("source image %q does not exist in project %q", sourceImage, sourceImageProject)
	}
	if sizeGb < srcImg.DiskSizeGb {
		return fmt.Errorf("requested size (%dGB) must be at least source image disk size (%dGB)", sizeGb, srcImg.DiskSizeGb)
	}

	if exists, err := h.ImageExists(destImageProject, imageName); err != nil || exists {
		if err != nil {
			return fmt.Errorf("failed to check if image %q exists: %w", imageName, err)
		}
		return fmt.Errorf("image %q already exists in project %q", imageName, destImageProject)
	}

	tempDisk := gce.GceResourceNameWithSuffix(imageName, "-resize-disk")
	if diskExists, err := h.DiskExists(project, zone, tempDisk); err != nil || diskExists {
		if err != nil {
			return fmt.Errorf("failed to check if disk %q exists: %w", tempDisk, err)
		}
		return fmt.Errorf("disk %q already exists in project %q zone %q", tempDisk, project, zone)
	}

	log.Printf("creating %dGB disk %q from %s/%s...", sizeGb, tempDisk, sourceImageProject, sourceImage)
	if _, err := h.CreateDisk(sourceImageProject, sourceImage, tempDisk, gce.CreateDiskOpts{SizeGb: sizeGb}); err != nil {
		return fmt.Errorf("failed to create disk: %w", err)
	}
	defer func() {
		log.Printf("cleanup: deleting disk %q...", tempDisk)
		if err := h.DeleteDisk(tempDisk); err != nil {
			log.Printf("cleanup: error deleting disk: %v", err)
		} else {
			log.Println("cleanup: disk deleted")
		}
	}()
	log.Printf("disk created: %q", tempDisk)

	log.Printf("creating image %q in project %q...", imageName, destImageProject)
	createImageOpts := gce.CreateImageOpts{
		DestProject: destImageProject,
		Labels:      srcImg.Labels,
	}
	if err := h.CreateImage(tempDisk, imageName, createImageOpts); err != nil {
		return fmt.Errorf("failed to create image: %w", err)
	}
	log.Printf("image created: %q", imageName)
	return nil
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
	if sizeGb <= 0 {
		log.Fatal("usage: `-size-gb` must be greater than 0")
	}
	if destImageProject == "" {
		destImageProject = project
	}
	if imageName == "" {
		imageName = defaultOutputImageName(sourceImage, sizeGb)
	}

	if err := resizeImage(project, zone, sourceImageProject, sourceImage, destImageProject, imageName, sizeGb); err != nil {
		log.Fatal(err)
	}
}
