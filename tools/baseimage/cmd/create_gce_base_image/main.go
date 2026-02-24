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

	"github.com/google/android-cuttlefish/tools/baseimage/pkg/gce"
	"github.com/google/android-cuttlefish/tools/baseimage/pkg/gce/scripts"
)

const mountpoint = "/mnt/image"

// Flags
var (
	project            string
	zone               string
	arch               string
	sourceImageProject string
	sourceImage        string
	imageName          string
)

func init() {
	flag.StringVar(&project, "project", "", "GCP project whose resources will be used for creating the amended image")
	flag.StringVar(&zone, "zone", "us-central1-a", "GCP zone used for creating relevant resources")
	flag.StringVar(&arch, "arch", "x86_64", "architecture of GCE image. Supports either x86_64 or arm64")
	flag.StringVar(&sourceImageProject, "source-image-project", "", "Source image GCP project")
	flag.StringVar(&sourceImage, "source-image", "", "Source image name")
	flag.StringVar(&imageName, "image-name", "", "output GCE image name")
}

type createImageOpts struct {
	Arch               gce.Arch
	SourceImageProject string
	SourceImage        string
	ImageName          string
}

func fillAvailableSpace(project, zone, insName string) error {
	return gce.RunCmd(project, zone, insName, "./fill_available_disk_space.sh")
}

func mountAttachedDisk(project, zone, insName string) error {
	return gce.RunCmd(project, zone, insName, "./mount_attached_disk.sh "+mountpoint)
}

func createImageMain(project, zone string, opts createImageOpts) error {
	h, err := gce.NewGceHelper(project, zone)
	if err != nil {
		return fmt.Errorf("failed to create GCE helper: %w", err)
	}
	insName := opts.ImageName
	attachedDiskName := fmt.Sprintf("%s-attached-disk", insName)
	defer func() {
		// DetachDisk
		log.Printf("cleanup: detaching disk %q from instance %q...", attachedDiskName, insName)
		if err := h.DetachDisk(insName, attachedDiskName); err != nil {
			log.Printf("cleanup: error detaching disk: %v", err)
		} else {
			log.Println("cleanup: disk detached")
		}
		// Delete Instance
		log.Printf("cleanup: deleting instance %q...", insName)
		if err := h.DeleteInstance(insName); err != nil {
			log.Printf("cleanup: error deleting instance: %v", err)
		} else {
			log.Println("cleanup: instance deleted")
		}
		// Delete Disk
		log.Printf("cleanup: deleting disk %q...", attachedDiskName)
		if err := h.DeleteDisk(attachedDiskName); err != nil {
			log.Printf("cleanup: error deleting disk: %v", err)
		} else {
			log.Println("cleanup: disk deleted")
		}
	}()
	log.Println("creating disk...")
	disk, err := h.CreateDisk(opts.SourceImageProject, opts.SourceImage, attachedDiskName, gce.CreateDiskOpts{})
	if err != nil {
		return fmt.Errorf("failed to create disk: %w", err)
	}
	log.Printf("disk created: %q", attachedDiskName)
	log.Println("creating instance...")
	ins, err := h.CreateInstance(insName, opts.Arch)
	if err != nil {
		return fmt.Errorf("failed to create instance: %w", err)
	}
	log.Printf("instance created: %q", insName)
	log.Println("attaching disk...")
	if err := h.AttachDisk(insName, attachedDiskName); err != nil {
		log.Fatalf("failed to attach disk %q to instance %q: %v", disk.Name, ins.Name, err)
	}
	log.Println("disk attached")

	if err := gce.WaitForInstance(project, zone, insName); err != nil {
		return fmt.Errorf("waiting for instance error: %v", err)
	}
	// Upload Scripts
	list := []struct {
		dstname string
		content string
	}{
		{"fill_available_disk_space.sh", scripts.FillAvailableDiskSpace},
		{"mount_attached_disk.sh", scripts.MountAttachedDisk},
		{"install_nvidia.sh", scripts.InstallNvidia},
		{"create_base_image_main.sh", scripts.CreateBaseImageMain},
	}
	for _, s := range list {
		if err := gce.UploadBashScript(project, zone, insName, s.dstname, s.content); err != nil {
			return fmt.Errorf("error uploading script: %v", err)
		}
	}
	// Execute Scripts
	if err := fillAvailableSpace(project, zone, insName); err != nil {
		return fmt.Errorf("fillAvailableSpace error: %v", err)
	}
	if err := mountAttachedDisk(project, zone, insName); err != nil {
		return fmt.Errorf("mountAttachedDisk error: %v", err)
	}
	if err := gce.RunCmd(project, zone, insName, "./create_base_image_main.sh"); err != nil {
		return err
	}
	// Reboot the instance to force a clean umount of the attached disk's file system.
	if err := gce.RunCmd(project, zone, insName, "sudo reboot"); err != nil {
		return err
	}
	if err := gce.WaitForInstance(project, zone, insName); err != nil {
		return fmt.Errorf("waiting for instance error: %v", err)
	}
	log.Printf("stopping instance %q...", insName)
	if err := h.StopInstance(insName); err != nil {
		return fmt.Errorf("error stopping instance: %v", err)
	}
	if err := h.CreateImage(insName, attachedDiskName, opts.ImageName); err != nil {
		return fmt.Errorf("failed to create image: %w", err)
	}
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
	if arch == "" {
		log.Fatal("usage: `-arch` must not be empty")
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
	architecture, err := gce.ParseArch(arch)
	if err != nil {
		log.Fatal(err)
	}

	opts := createImageOpts{
		Arch:               architecture,
		SourceImageProject: sourceImageProject,
		SourceImage:        sourceImage,
		ImageName:          imageName,
	}
	if err := createImageMain(project, zone, opts); err != nil {
		log.Fatal(err)
	}
	log.Printf("image %q was created successfully", imageName)
}
