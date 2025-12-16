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
	"os"
	"path/filepath"
	"slices"
	"strings"
	"time"

	"github.com/google/android-cuttlefish/tools/baseimage/pkg/gce"
	"github.com/google/android-cuttlefish/tools/baseimage/pkg/gce/scripts"
)

const (
	mountpoint                        = "/mnt/image"
	defaultsCuttlefishIntegrationFile = "/etc/defaults/cuttlefish-integration"
)

type DebSrcsFlag struct {
	Srcs []string
}

func (v *DebSrcsFlag) String() string {
	return strings.Join(v.Srcs, " ")
}

func (v *DebSrcsFlag) Set(s string) error {
	_, err := os.Stat(s)
	if err != nil {
		return fmt.Errorf("invalid path: %w", err)
	}
	if !slices.Contains(v.Srcs, s) {
		v.Srcs = append(v.Srcs, s)
	}
	return nil
}

var (
	project                             = flag.String("project", "", "GCP project whose resources will be used for creating the amended image")
	zone                                = flag.String("zone", "us-central1-a", "GCP zone used for creating relevant resources")
	source_image_project                = flag.String("source-image-project", "", "Source image GCP project")
	source_image                        = flag.String("source-image", "", "Source image name")
	image_name                          = flag.String("image-name", "", "output GCE image name")
	deb_srcs                            = DebSrcsFlag{}
	defaults_cuttlefish_integration_src = flag.String("defaults_cuttlefish_integration_src", "", "Path to file which will be written to /etc/defaults/cuttlefish-integration.")
)

func init() {
	flag.Var(&deb_srcs, "deb", "local path to debian package")
}

type amendImageOpts struct {
	SourceImageProject string
	SourceImage        string
	ImageName          string
	DebSrcs            []string
}

func uploadScripts(project, zone, insName string) error {
	list := []struct {
		dstname string
		content string
	}{
		{"fill_available_disk_space.sh", scripts.FillAvailableDiskSpace},
		{"mount_attached_disk.sh", scripts.MountAttachedDisk},
		{"install.sh", scripts.InstallCuttlefishPackages},
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

func installCuttlefishDebs(project, zone, insName string, debSrcs []string) error {
	dstSrcs := []string{}
	for _, src := range debSrcs {
		dst := "/tmp/" + filepath.Base(src)
		dstSrcs = append(dstSrcs, dst)
		if err := gce.UploadFile(project, zone, insName, src, dst); err != nil {
			return fmt.Errorf("error uploading %s: %v", src, err)
		}
	}
	args := strings.Join(dstSrcs, " ")
	if err := gce.RunCmd(project, zone, insName, "./install.sh "+args); err != nil {
		return err
	}
	return nil
}

func maybeSetDefaultsCuttlefishIntegrationFile(project, zone, insName string) error {
	if *defaults_cuttlefish_integration_src != "" {
		// The disk mountpoint is at /mnt/image, mounted in `installCuttlefishDebs`, and is not unmounted.
		if err := gce.UploadFile(project, zone, insName, *defaults_cuttlefish_integration_src, mountpoint+defaultsCuttlefishIntegrationFile); err != nil {
			return err
		}
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
	_, err = h.CreateInstance(insName, gce.ArchX86)
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

	if err := installCuttlefishDebs(project, zone, insName, opts.DebSrcs); err != nil {
		return fmt.Errorf("install cuttlefish debs error: %v", err)
	}

	if err := maybeSetDefaultsCuttlefishIntegrationFile(project, zone, insName); err != nil {
		return fmt.Errorf("couldn't set %s: %v", defaultsCuttlefishIntegrationFile, err)
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
		log.Fatal("usage: `-source-image` must not be empty")
	}
	if *image_name == "" {
		log.Fatal("usage: `-image-name` must not be empty")
	}
	if len(deb_srcs.Srcs) == 0 {
		log.Fatal("usage: `-deb` must not be empty")
	}

	opts := amendImageOpts{
		SourceImageProject: *source_image_project,
		SourceImage:        *source_image,
		ImageName:          *image_name,
		DebSrcs:            deb_srcs.Srcs,
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
		*image_name,
	)
}
