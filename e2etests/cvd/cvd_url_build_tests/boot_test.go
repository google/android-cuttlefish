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
	"fmt"
	"os"
	"path"
	"path/filepath"
	"testing"

	"github.com/google/android-cuttlefish/e2etests/cvd/common"
)

const bootBuild = "aosp-android-latest-release/aosp_cf_x86_64_only_phone-userdebug"

const urlLoadConfig = `
{
  "instances": [
    {
      "name": "ins-1",
      "disk": {
        "default_build": "%s"
      },
      "vm": {
        "cpus": 4,
        "memory_mb": 4096,
        "setupwizard_mode": "REQUIRED"
      },
      "graphics": {
        "displays": [
          {
            "width": 720,
            "height": 1280,
            "dpi": 140,
            "refresh_rate_hertz": 60
          }
        ],
        "record_screen": false
      }
    }
  ],
  "netsim_bt": false,
  "metrics": {
    "enable": true
  },
  "common": {
    "host_package": "%s"
  }
}`

// Downloads the archives of an Android build server build without unpacking
// them, so that they can be served again as a plain https:// build.
func stageBuild(t *testing.T, c *e2etests.TestContext, directory string) string {
	fetchCmd := []string{
		c.TargetBin(),
		"fetch",
		"--keep_downloaded_archives",
		"--target_directory=" + directory,
		"--default_build=" + bootBuild,
	}
	if credential := os.Getenv("CREDENTIAL_SOURCE"); credential != "" {
		fetchCmd = append(fetchCmd, "--credential_source="+credential)
	}
	if _, err := c.RunCmd(fetchCmd...); err != nil {
		t.Fatal(err)
	}

	matches, err := filepath.Glob(path.Join(directory, "*-img-*.zip"))
	if err != nil {
		t.Fatal(err)
	}
	if len(matches) != 1 {
		t.Fatalf("staged %d image zips in %s, want exactly one", len(matches), directory)
	}
	if !e2etests.FileExists(path.Join(directory, hostPackageName)) {
		t.Fatalf("%s is missing from %s", hostPackageName, directory)
	}
	return path.Base(matches[0])
}

// Boots a device from the same artifacts the other tests in this suite take
// from the Android build servers, served over https:// instead.
func TestCvdLoadHttpsObjectBuild(t *testing.T) {
	c := e2etests.TestContext{}
	c.SetUp(t)
	defer c.TearDown()

	staging := t.TempDir()
	imgZip := stageBuild(t, &c, staging)
	base, certpath := serveArtifacts(t, staging)

	config := fmt.Sprintf(urlLoadConfig, base+"/"+imgZip, base+"/"+hostPackageName)
	if err := c.CVDCreateWithConfigFile(e2etests.LoadArgs{
		LoadConfig: config,
		Env:        map[string]string{"CURL_CA_BUNDLE": certpath},
	}); err != nil {
		t.Fatal(err)
	}

	if err := c.RunAdbWaitForDevice(); err != nil {
		t.Fatal(err)
	}
}
