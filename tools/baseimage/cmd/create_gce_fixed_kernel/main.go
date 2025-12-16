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

// Cuttlefish base images are based on debian images.
const (
	debianSourceImageProject = "debian-cloud"
	debianSourceImage        = "debian-13-trixie-v20251014"
)

const mountpoint = "/mnt/image"

var (
	project       = flag.String("project", "", "GCE project whose resources will be used for creating the image")
	zone          = flag.String("zone", "us-central1-a", "GCE zone used for creating relevant resources")
	linuxImageDeb = flag.String("linux-image-deb", "", "linux-image-* package name. E.g. linux-image-6.1.0-40-cloud-amd64")
	imageName     = flag.String("image-name", "", "output GCE image name")
)

func mountAttachedDisk(project, zone, insName string) error {
	return gce.RunCmd(project, zone, insName, "./mount_attached_disk.sh "+mountpoint)
}

type kernelImageOpts struct {
	LinuxImageDeb string
	ImageName     string
}

func createImageMain(project, zone string, opts kernelImageOpts) error {
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
	disk, err := h.CreateDisk(debianSourceImageProject, debianSourceImage, attachedDiskName, gce.CreateDiskOpts{})
	if err != nil {
		return fmt.Errorf("failed to create disk: %w", err)
	}
	log.Printf("disk created: %q", attachedDiskName)
	log.Println("creating instance...")
	ins, err := h.CreateInstance(insName)
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
		{"mount_attached_disk.sh", scripts.MountAttachedDisk},
		{"install_kernel_main.sh", scripts.InstallKernelMain},
	}
	for _, s := range list {
		if err := gce.UploadBashScript(project, zone, insName, s.dstname, s.content); err != nil {
			return fmt.Errorf("error uploading script: %v", err)
		}
	}
	// Execute Scripts
	if err := mountAttachedDisk(project, zone, insName); err != nil {
		return fmt.Errorf("mountAttachedDisk error: %v", err)
	}
	if err := gce.RunCmd(project, zone, insName, "./install_kernel_main.sh "+opts.LinuxImageDeb); err != nil {
		return err
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

	if *project == "" {
		log.Fatal("usage: `-project` must not be empty")
	}
	if *zone == "" {
		log.Fatal("usage: `-zone` must not be empty")
	}
	if *linuxImageDeb == "" {
		log.Fatal("usage: `-linux-image-deb` must not be empty")
	}
	if *imageName == "" {
		log.Fatal("usage: `-image-name` must not be empty")
	}

	opts := kernelImageOpts{
		LinuxImageDeb: *linuxImageDeb,
		ImageName:     *imageName,
	}
	if err := createImageMain(*project, *zone, opts); err != nil {
		log.Fatal(err)
	}
	log.Printf("image %q was created successfully", *imageName)
	fmt.Printf(`Copy the image somewhere else:
gcloud compute images create \
  --source-image-project=%s \
  --source-image=%s \
  --project=[DEST_PROJECT] \
  --family=[DEST_IMAGE_FAMILY] [DEST_IMAGE_NAME]
`,
		*project,
		*imageName,
	)
}
