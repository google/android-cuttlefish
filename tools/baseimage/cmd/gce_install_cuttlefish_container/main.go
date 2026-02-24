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
	"github.com/google/android-cuttlefish/tools/baseimage/pkg/gce/scripts"
)

const (
	mountpoint                        = "/mnt/image"
	defaultsCuttlefishIntegrationFile = "/etc/defaults/cuttlefish-integration"
)

// Flags
var (
	project            string
	zone               string
	arch               string
	sourceImageProject string
	sourceImage        string
	imageName          string
	containerImageSrc  string
)

func init() {
	flag.StringVar(&project, "project", "", "GCP project whose resources will be used for creating the amended image")
	flag.StringVar(&zone, "zone", "us-central1-a", "GCP zone used for creating relevant resources")
	flag.StringVar(&arch, "arch", "x86_64", "architecture of GCE image. Supports either x86_64 or arm64")
	flag.StringVar(&sourceImageProject, "source-image-project", "", "Source image GCP project")
	flag.StringVar(&sourceImage, "source-image", "", "Source image name")
	flag.StringVar(&imageName, "image-name", "", "output GCE image name")
	flag.StringVar(&containerImageSrc, "container-image-src", "", "local path to container image")
}

type amendImageOpts struct {
	Arch               gce.Arch
	SourceImageProject string
	SourceImage        string
	ImageName          string
	ContainerImageSrc  string
}

func uploadScripts(project, zone, insName string) error {
	list := []struct {
		dstname string
		content string
	}{
		{"fill_available_disk_space.sh", scripts.FillAvailableDiskSpace},
		{"mount_attached_disk.sh", scripts.MountAttachedDisk},
		{"load_cuttlefish_container_image.sh", scripts.LoadCuttlefishContainerImage},
	}
	for _, s := range list {
		if err := gce.UploadBashScript(project, zone, insName, s.dstname, s.content); err != nil {
			return fmt.Errorf("error uploading bash script: %v", err)
		}
	}
	return nil
}

func fillAvailableSpace(project, zone, insName string) error {
	return gce.RunCmd(project, zone, insName, "./fill_available_disk_space.sh")
}

func mountAttachedDisk(project, zone, insName string) error {
	return gce.RunCmd(project, zone, insName, "./mount_attached_disk.sh "+mountpoint)
}

func loadCuttlefishContainerImage(project, zone, insName string, imageSrc string) error {
	dst := "/tmp/" + filepath.Base(imageSrc)
	if err := gce.UploadFile(project, zone, insName, imageSrc, dst); err != nil {
		return fmt.Errorf("error uploading %s: %v", imageSrc, err)
	}
	if err := gce.RunCmd(project, zone, insName, "./load_cuttlefish_container_image.sh "+dst); err != nil {
		return err
	}
	return nil
}

func cleanupDeleteDisk(h *gce.GceHelper, disk string) {
	log.Printf("cleanup: deleting disk %q...", disk)
	if err := h.DeleteDisk(disk); err != nil {
		log.Printf("cleanup: error deleting disk: %v", err)
	} else {
		log.Println("cleanup: disk deleted")
	}
}

func cleanupDeleteInstance(h *gce.GceHelper, ins string) {
	log.Printf("cleanup: deleting instance %q...", ins)
	if err := h.DeleteInstance(ins); err != nil {
		log.Printf("cleanup: error deleting instance: %v", err)
	} else {
		log.Println("cleanup: instance deleted")
	}
}

func cleanupDetachDisk(h *gce.GceHelper, ins, disk string) {
	log.Printf("cleanup: detaching disk %q from instance %q...", ins, disk)
	if err := h.DetachDisk(ins, disk); err != nil {
		log.Printf("cleanup: error detaching disk: %v", err)
	} else {
		log.Println("cleanup: disk detached")
	}
}

func amendImageMain(project, zone string, opts amendImageOpts) error {
	h, err := gce.NewGceHelper(project, zone)
	if err != nil {
		return fmt.Errorf("failed to create GCE helper: %w", err)
	}
	insName := opts.ImageName
	attachedDiskName := fmt.Sprintf("%s-attached-disk", insName)

	log.Println("creating disk...")
	createDiskOpts := gce.CreateDiskOpts{SizeGb: 32}
	_, err = h.CreateDisk(
		opts.SourceImageProject, opts.SourceImage, attachedDiskName, createDiskOpts)
	if err != nil {
		return fmt.Errorf("failed to create disk: %w", err)
	}
	log.Printf("disk created: %q", attachedDiskName)
	defer cleanupDeleteDisk(h, attachedDiskName)

	log.Println("creating instance...")
	_, err = h.CreateInstance(insName, opts.Arch)
	if err != nil {
		return fmt.Errorf("failed to create instance: %w", err)
	}
	log.Printf("instance created: %q", insName)
	defer cleanupDeleteInstance(h, insName)

	log.Println("attaching disk...")
	if err := h.AttachDisk(insName, attachedDiskName); err != nil {
		log.Fatalf("failed to attach disk %q to instance %q: %v", attachedDiskName, insName, err)
	}
	log.Println("disk attached")
	defer cleanupDetachDisk(h, insName, attachedDiskName)

	if err := gce.WaitForInstance(project, zone, insName); err != nil {
		return fmt.Errorf("waiting for instance error: %v", err)
	}

	if err := uploadScripts(project, zone, insName); err != nil {
		return fmt.Errorf("uploadScripts error: %v", err)
	}

	if err := fillAvailableSpace(project, zone, insName); err != nil {
		return fmt.Errorf("fillAvailableSpace error: %v", err)
	}

	if err := mountAttachedDisk(project, zone, insName); err != nil {
		return fmt.Errorf("mountAttachedDisk error: %v", err)
	}

	if err := loadCuttlefishContainerImage(project, zone, insName, opts.ContainerImageSrc); err != nil {
		return fmt.Errorf("install cuttlefish container error: %v", err)
	}

	// Reboot the instance to force a clean umount of the attached disk's file system.
	if err := gce.RunCmd(project, zone, insName, "sudo reboot"); err != nil {
		return err
	}
	if err := gce.WaitForInstance(project, zone, insName); err != nil {
		return fmt.Errorf("waiting for instance error: %v", err)
	}
	log.Printf("deleting instance %q...", insName)
	if err := h.StopInstance(insName); err != nil {
		return fmt.Errorf("error deleting instance: %v", err)
	}
	log.Println("instance deleted")

	log.Printf("creating image %q...", opts.ImageName)
	if err := h.CreateImage(insName, attachedDiskName, opts.ImageName); err != nil {
		return fmt.Errorf("failed to create image: %w", err)
	}
	log.Println("image created")
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
	if containerImageSrc == "" {
		log.Fatal("usage: `-container-image-src` must not be empty")
	}
	architecture, err := gce.ParseArch(arch)
	if err != nil {
		log.Fatal(err)
	}

	opts := amendImageOpts{
		Arch:               architecture,
		SourceImageProject: sourceImageProject,
		SourceImage:        sourceImage,
		ImageName:          imageName,
		ContainerImageSrc:  containerImageSrc,
	}
	if err := amendImageMain(project, zone, opts); err != nil {
		log.Fatal(err)
	}
	fmt.Printf(`Copy the image somewhere else:
gcloud compute images create \
  --source-image-project=%s \
  --source-image=%s \
  --project=[DEST_PROJECT] \
  --family=[DEST_IMAGE_FAMILY] [DEST_IMAGE_NAME]
`,
		project,
		imageName,
	)
}
