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
	"time"

	"github.com/google/android-cuttlefish/tools/baseimage/pkg/gce"
	"github.com/google/android-cuttlefish/tools/baseimage/pkg/gce/scripts"
)

const (
	mountpoint = "/mnt/image"

	cuttlefishHODefaultsPath = "/etc/default/cuttlefish-host_orchestrator"
)

var (
	project                    = flag.String("project", "", "GCP project whose resources will be used for creating the amended image")
	zone                       = flag.String("zone", "us-central1-a", "GCP zone used for creating relevant resources")
	source_image_project       = flag.String("source-image-project", "", "Source image GCP project")
	source_image               = flag.String("source-image", "", "Source image name")
	image_name                 = flag.String("image-name", "", "output GCE image name")
	cuttlefish_ho_defaults_src = flag.String("cuttlefish-ho-defaults-src", "", "Local path to custom HO defaults")
)

type createImageOpts struct {
	SourceImageProject      string
	SourceImage             string
	ImageName               string
	CuttlefishHODefaultsSrc string
}

func mountAttachedDisk(project, zone, insName string) error {
	return gce.RunCmd(project, zone, insName, "./mount_attached_disk.sh "+mountpoint)
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

func createImage(project, zone string, opts createImageOpts) error {
	h, err := gce.NewGceHelper(project, zone)
	if err != nil {
		return fmt.Errorf("failed to create GCE helper: %w", err)
	}
	insName := opts.ImageName
	attachedDiskName := fmt.Sprintf("%s-attached-disk", insName)

	log.Println("creating disk...")
	if _, err := h.CreateDisk(opts.SourceImageProject, opts.SourceImage, attachedDiskName, gce.CreateDiskOpts{}); err != nil {
		return fmt.Errorf("failed to create disk: %w", err)
	}
	defer cleanupDeleteDisk(h, attachedDiskName)
	log.Printf("disk created: %q", attachedDiskName)

	log.Println("creating instance...")
	if _, err := h.CreateInstance(insName, gce.ArchX86); err != nil {
		return fmt.Errorf("failed to create instance: %w", err)
	}
	defer cleanupDeleteInstance(h, insName)
	log.Printf("instance created: %q", insName)

	log.Println("attaching disk...")
	if err := h.AttachDisk(insName, attachedDiskName); err != nil {
		log.Fatalf("failed to attach disk %q to instance %q: %v", attachedDiskName, insName, err)
	}
	defer cleanupDetachDisk(h, insName, attachedDiskName)
	log.Println("disk attached")

	if err := gce.WaitForInstance(project, zone, insName); err != nil {
		return fmt.Errorf("waiting for instance error: %v", err)
	}

	if err := gce.UploadBashScript(project, zone, insName, "mount_attached_disk.sh", scripts.MountAttachedDisk); err != nil {
		return fmt.Errorf("error uploading script: %v", err)
	}
	if err := mountAttachedDisk(project, zone, insName); err != nil {
		return fmt.Errorf("mountAttachedDisk error: %v", err)
	}

	if opts.CuttlefishHODefaultsSrc != "" {
		const tmpDst = "/tmp/cuttlefish-host_orchestrator"
		if err := gce.UploadFile(project, zone, insName, opts.CuttlefishHODefaultsSrc, tmpDst); err != nil {
			return err
		}
		cmd := fmt.Sprintf("sudo cp %s %s", tmpDst, mountpoint+cuttlefishHODefaultsPath)
		if err := gce.RunCmd(project, zone, insName, cmd); err != nil {
			return err
		}
	}

	// Reboot the instance to force a clean umount of the attached disk's file system.
	if err := gce.RunCmd(project, zone, insName, "sudo reboot"); err != nil {
		return err
	}
	time.Sleep(2 * time.Minute)
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
  -cuttlefish-ho-defaults-src SRC
`
}

func main() {

	flag.Parse()

	if *project == "" {
		log.Fatal(usage("`-project` must not be empty"))
	}
	if *zone == "" {
		log.Fatal(usage("`-zone` must not be empty"))
	}
	if *source_image_project == "" {
		log.Fatal(usage("`-source-image-project` must not be empty"))
	}
	if *source_image == "" {
		log.Fatal(usage("`-source-image` must not be empty"))
	}
	if *image_name == "" {
		log.Fatal(usage("`-image-name` must not be empty"))
	}
	if *cuttlefish_ho_defaults_src == "" {
		log.Fatal(usage("`-cuttlefish-ho-defaults-src` must not be empty"))
	}

	opts := createImageOpts{
		SourceImageProject:      *source_image_project,
		SourceImage:             *source_image,
		ImageName:               *image_name,
		CuttlefishHODefaultsSrc: *cuttlefish_ho_defaults_src,
	}

	if err := createImage(*project, *zone, opts); err != nil {
		log.Fatal(err)
	}
}
