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
	"errors"
	"flag"
	"fmt"
	"io/fs"
	"log"
	"os"
	"path/filepath"
	"regexp"
	"slices"
	"strings"
	"time"

	"github.com/google/android-cuttlefish/tools/baseimage/pkg/cli"
	"github.com/google/android-cuttlefish/tools/baseimage/pkg/gce"
	"github.com/google/android-cuttlefish/tools/baseimage/pkg/gce/scripts"
)

const cuttlefishHODefaultsPath = "/etc/default/cuttlefish-host_orchestrator"

var cuttlefishDebPackages = []string{
	"cuttlefish-base",
	"cuttlefish-defaults",
	"cuttlefish-integration",
	"cuttlefish-metrics",
	"cuttlefish-user",
	"cuttlefish-orchestration",
}

var baseDebVersionRegex = regexp.MustCompile(`^cuttlefish-base_([0-9]+)\.([0-9]+)`)

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

// Flags
var (
	project                 string
	zone                    string
	arch                    cli.Arch
	sourceImageProject      string
	sourceImage             string
	destImageProject        string
	imageName               string
	debsDir                 string
	debSrcs                 DebSrcsFlag
	useGceMetadata          bool
	cuttlefishHODefaultsSrc string
)

func init() {
	flag.StringVar(&project, "project", "", "GCP project whose resources will be used for creating the amended image")
	flag.StringVar(&zone, "zone", "us-central1-a", "GCP zone used for creating relevant resources")
	flag.Var(&arch, "arch", "architecture of GCE image. Supports either x86_64 or arm64")
	flag.StringVar(&sourceImageProject, "source-image-project", "", "Source image GCP project")
	flag.StringVar(&sourceImage, "source-image", "", "Source image name")
	flag.StringVar(&destImageProject, "dest-image-project", "", "Destination image GCP project (defaults to -project)")
	flag.StringVar(&imageName, "image-name", "", "output GCE image name (defaults to cuttlefish-image-<timestamp>--cvd<major>-<minor>)")
	flag.StringVar(&debsDir, "debs-dir", "", "local directory containing cuttlefish debian packages")
	flag.Var(&debSrcs, "deb", "local path to debian package")
	flag.BoolVar(&useGceMetadata, "use-gce-metadata", true, "Configure Host Orchestrator to use GCE metadata for Build API credentials by default")
	flag.StringVar(&cuttlefishHODefaultsSrc, "cuttlefish-ho-defaults-src", "", "Local path to custom HO defaults (overrides -use-gce-metadata)")
}

func debArchForGceArch(a gce.Arch) (string, error) {
	switch a {
	case gce.ArchX86:
		return "amd64", nil
	case gce.ArchArm:
		return "arm64", nil
	default:
		return "", fmt.Errorf("unsupported arch: %v", a)
	}
}

func findCuttlefishDebs(dir string, a gce.Arch) ([]string, error) {
	debArch, err := debArchForGceArch(a)
	if err != nil {
		return nil, err
	}
	matchesByPkg := make(map[string][]string, len(cuttlefishDebPackages))
	err = filepath.WalkDir(dir, func(path string, d fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if d.IsDir() {
			return nil
		}
		name := d.Name()
		suffix := "_" + debArch + ".deb"
		if !strings.HasSuffix(name, suffix) {
			return nil
		}
		for _, pkg := range cuttlefishDebPackages {
			if strings.HasPrefix(name, pkg+"_") {
				matchesByPkg[pkg] = append(matchesByPkg[pkg], path)
				break
			}
		}
		return nil
	})
	if err != nil {
		return nil, fmt.Errorf("failed to scan debs directory %q: %w", dir, err)
	}

	result := make([]string, 0, len(cuttlefishDebPackages))
	for _, pkg := range cuttlefishDebPackages {
		matches := matchesByPkg[pkg]
		if len(matches) == 0 {
			return nil, fmt.Errorf("missing %s package (*_%s.deb) in %q", pkg, debArch, dir)
		}
		if len(matches) > 1 {
			return nil, fmt.Errorf("multiple %s packages found in %q: %v", pkg, dir, matches)
		}
		result = append(result, matches[0])
	}
	return result, nil
}

func defaultReleaseImageName(debs []string, now time.Time) (string, error) {
	for _, deb := range debs {
		base := filepath.Base(deb)
		if m := baseDebVersionRegex.FindStringSubmatch(base); m != nil {
			ts := now.UTC().Format("20060102150405")
			return fmt.Sprintf("cuttlefish-image-%s--cvd%s-%s", ts, m[1], m[2]), nil
		}
	}
	return "", errors.New("unable to infer cvd version from cuttlefish-base deb filename; specify `-image-name` explicitly")
}

func resolveDebs(dir string, explicitDebs []string, a gce.Arch) ([]string, error) {
	if dir != "" && len(explicitDebs) > 0 {
		return nil, errors.New("specify either `-debs-dir` or `-deb`, not both")
	}
	if dir != "" {
		return findCuttlefishDebs(dir, a)
	}
	if len(explicitDebs) == 0 {
		return nil, errors.New("either `-debs-dir` or `-deb` must not be empty")
	}
	return explicitDebs, nil
}

func uploadConfig(project, zone, insName, src, dst string) error {
	tmp := filepath.Join("/tmp", filepath.Base(src))
	if err := gce.UploadFile(project, zone, insName, src, tmp); err != nil {
		return err
	}
	cmd := fmt.Sprintf("sudo install -m 0644 -o root -g root %s %s", tmp, filepath.Join(gce.BuildImageMountPoint, dst))
	return gce.RunCmd(project, zone, insName, cmd)
}

func enableGceMetadataCmd(hoDefaultsFile string) string {
	return fmt.Sprintf(
		"if [ -f %s ]; then sudo sed -i 's|^#*[[:space:]]*build_api_credentials_use_gce_metadata=.*|build_api_credentials_use_gce_metadata=true|' %s && grep -qx 'build_api_credentials_use_gce_metadata=true' %s; fi",
		hoDefaultsFile, hoDefaultsFile, hoDefaultsFile,
	)
}

func main() {
	flag.Parse()

	if arch.String() == "" {
		log.Fatal("usage: `-arch` must not be empty")
	}
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
	if destImageProject == "" {
		destImageProject = project
	}
	if cuttlefishHODefaultsSrc != "" {
		if _, err := os.Stat(cuttlefishHODefaultsSrc); err != nil {
			log.Fatalf("invalid -cuttlefish-ho-defaults-src path: %v", err)
		}
	}

	resolvedDebs, err := resolveDebs(debsDir, debSrcs.Srcs, arch.GceArch())
	if err != nil {
		log.Fatalf("usage: %v", err)
	}

	if imageName == "" {
		imageName, err = defaultReleaseImageName(resolvedDebs, time.Now())
		if err != nil {
			log.Fatalf("usage: %v", err)
		}
		log.Printf("inferred output image name: %q", imageName)
	}

	buildImageOpts := gce.BuildImageOpts{
		Arch:                   arch.GceArch(),
		SourceImageProject:     sourceImageProject,
		SourceImage:            sourceImage,
		DestImageProject:       destImageProject,
		ImageName:              imageName,
		Labels:                 map[string]string{"base_image": sourceImage},
		CreateAttachedDiskOpts: gce.CreateDiskOpts{SizeGb: 32},
		ModifyFunc: func(project, zone, insName string) error {
			dstSrcs := []string{}
			for _, src := range resolvedDebs {
				dst := "/tmp/" + filepath.Base(src)
				dstSrcs = append(dstSrcs, dst)
				if err := gce.UploadFile(project, zone, insName, src, dst); err != nil {
					return fmt.Errorf("error uploading %s: %v", src, err)
				}
			}
			if err := gce.UploadBashScript(project, zone, insName, "install_cuttlefish_debs.sh", scripts.InstallCuttlefishDebs); err != nil {
				return fmt.Errorf("error uploading bash script: %v", err)
			}
			args := strings.Join(dstSrcs, " ")
			if err := gce.RunCmd(project, zone, insName, "./install_cuttlefish_debs.sh "+args); err != nil {
				return err
			}

			if cuttlefishHODefaultsSrc != "" {
				if err := uploadConfig(project, zone, insName, cuttlefishHODefaultsSrc, cuttlefishHODefaultsPath); err != nil {
					return fmt.Errorf("error uploading custom HO config: %w", err)
				}
			} else if useGceMetadata {
				hoFile := filepath.Join(gce.BuildImageMountPoint, cuttlefishHODefaultsPath)
				if err := gce.RunCmd(project, zone, insName, enableGceMetadataCmd(hoFile)); err != nil {
					return fmt.Errorf("error enabling GCE metadata in %s: %w", cuttlefishHODefaultsPath, err)
				}
			}
			return nil
		},
	}

	h, err := gce.NewGceHelper(project, zone)
	if err != nil {
		log.Fatal(err)
	}
	if err := h.BuildImage(project, zone, buildImageOpts); err != nil {
		log.Fatal(err)
	}
}
