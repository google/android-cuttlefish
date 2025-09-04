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
	"path/filepath"
	"time"

	"github.com/google/android-cuttlefish/tools/baseimage/pkg/gce"
	"github.com/google/android-cuttlefish/tools/baseimage/pkg/gce/scripts"
)

const (
	outImageName = "amended-image"
)

var (
	project                    = flag.String("project", "", "GCP project whose resources will be used for creating the amended image")
	zone                       = flag.String("zone", "us-central1-a", "GCP zone used for creating relevant resources")
	source_image_project       = flag.String("source-image-project", "", "Source image GCP project")
	source_image               = flag.String("source-image", "", "Source image name")
	cuttlefish_debs_zip_source = flag.String("cuttlefish-debs-zip-source", "", "Local path to zip file containing cuttlefish debian packages")
)

type amendImageOpts struct {
	SourceImageProject      string
	SourceImage             string
	CuttlefishDebsZipSource string
}

func installCuttlefishDebs(project, zone, insName, zipSrc string) error {
	dstSrc := "/tmp/" + filepath.Base(zipSrc)
	if err := gce.UploadFile(project, zone, insName, zipSrc, dstSrc); err != nil {
		return fmt.Errorf("error uploading %s: %v", zipSrc, err)
	}
	if err := gce.UploadBashScript(project, zone, insName, "install.sh", scripts.InstallCuttlefishPackages); err != nil {
		return fmt.Errorf("error uploading script: %v", err)
	}
	if err := gce.RunCmd(project, zone, insName, "./install.sh "+dstSrc); err != nil {
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
	insName := outImageName
	attachedDiskName := fmt.Sprintf("%s-attached-disk", insName)

	log.Println("creating disk...")
	_, err = h.CreateDisk(opts.SourceImageProject, opts.SourceImage, attachedDiskName)
	if err != nil {
		return fmt.Errorf("failed to create disk: %w", err)
	}
	log.Printf("disk created: %q", attachedDiskName)
	defer cleanupDeleteDisk(h, attachedDiskName)

	log.Println("creating instance...")
	_, err = h.CreateInstanceWithImage(opts.SourceImageProject, opts.SourceImage, "n1-standard-16", insName)
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

	installCuttlefishDebs(project, zone, insName, opts.CuttlefishDebsZipSource)

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

	log.Printf("creating image %q...", outImageName)
	if err := h.CreateImage(attachedDiskName, outImageName, nil); err != nil {
		return fmt.Errorf("failed to create image: %w", err)
	}
	log.Println("image created")
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
	if *source_image_project == "" {
		log.Fatal("usage: `-source-image-project` must not be empty")
	}
	if *source_image == "" {
		log.Fatal("usage: `-source_image` must not be empty")
	}
	if *cuttlefish_debs_zip_source == "" {
		log.Fatal("usage: `-cuttlefish-debs-zip-source` must not be empty")
	}

	opts := amendImageOpts{
		SourceImageProject:      *source_image_project,
		SourceImage:             *source_image,
		CuttlefishDebsZipSource: *cuttlefish_debs_zip_source,
	}
	if err := amendImageMain(*project, *zone, opts); err != nil {
		log.Fatal(err)
	}
	fmt.Printf(`Copy the image somewhere else:
gcloud compute images create \
  --source-image-project=%s \
  --source-image=%s \
  --project=[DEST_PROJECT] \
  --family=[DEST_IMAGE_FAMILY] [DEST_IMAGE_NAME]
`,
		*project,
		outImageName,
	)
}
