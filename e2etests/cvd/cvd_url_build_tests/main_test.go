// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package main

import (
	"os"
	"path"
	"strings"
	"testing"

	"github.com/google/android-cuttlefish/e2etests/cvd/common"
)

var hostPackageFiles = []string{"bin/launch_cvd", "bin/stop_cvd"}

func checkFetched(t *testing.T, directory string, names []string) {
	for _, name := range names {
		if !e2etests.FileExists(path.Join(directory, name)) {
			t.Errorf("%s is missing from %s", name, directory)
		}
	}
}

func fetch(t *testing.T, c *e2etests.TestContext, certpath string, args []string) {
	command := append([]string{c.TargetBin(), "fetch", "--enable_caching=false"}, args...)
	if _, err := c.RunCmdWithEnv(command, map[string]string{"CURL_CA_BUNDLE": certpath}); err != nil {
		t.Fatal(err)
	}
}

// Fetches a build named as a single https:// object. The image zip is the
// build's only artifact, so the host package has to be named separately.
func TestFetchHttpsObjectBuild(t *testing.T) {
	c := e2etests.TestContext{}
	c.SetUp(t)
	defer c.TearDown()

	const imgZip = "aosp_cf_x86_64_phone-img-13579246.zip"
	const query = "?fake_signature=not-a-real-credential"
	images := []string{"boot.img", "super.img", "vbmeta.img"}

	staging := t.TempDir()
	writeZip(t, path.Join(staging, imgZip), images)
	writeTarGz(t, path.Join(staging, hostPackageName), hostPackageFiles)
	base, certpath := serveArtifacts(t, staging)

	target := t.TempDir()
	fetch(t, &c, certpath, []string{
		"--target_directory=" + target,
		"--default_build=" + base + "/" + imgZip + query,
		"--host_package_build=" + base + "/" + hostPackageName,
	})

	checkFetched(t, target, images)
	checkFetched(t, target, hostPackageFiles)

	config := readFetcherConfig(t, target)
	for _, image := range images {
		checkProvenance(t, config, image, "default_build", base+"/"+imgZip)
	}

	// The query carries the credential of a pre-signed URL, so it belongs in
	// no record of the fetch.
	if strings.Contains(readFile(t, path.Join(target, "fetch.log")), query) {
		t.Errorf("fetch.log holds the query string of the build URL")
	}
}

// Fetches individually named artifacts out of an https:// directory. The
// directory cannot be listed, so every artifact is named by the build string.
func TestFetchHttpsDirectoryBuild(t *testing.T) {
	c := e2etests.TestContext{}
	c.SetUp(t)
	defer c.TearDown()

	staging := path.Join(t.TempDir(), "dist")
	if err := os.MkdirAll(staging, 0755); err != nil {
		t.Fatal(err)
	}
	writeArtifact(t, path.Join(staging, "boot.img"))
	writeTarGz(t, path.Join(staging, hostPackageName), hostPackageFiles)
	base, certpath := serveArtifacts(t, path.Dir(staging))
	directory := base + "/dist/"

	target := t.TempDir()
	fetch(t, &c, certpath, []string{
		"--target_directory=" + target,
		"--boot_build=" + directory + "{boot.img}",
		"--host_package_build=" + directory,
	})

	checkFetched(t, target, []string{"boot.img"})
	checkFetched(t, target, hostPackageFiles)

	checkProvenance(t, readFetcherConfig(t, target), "boot.img", "boot_build", directory)
}
