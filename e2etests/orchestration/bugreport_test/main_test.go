// Copyright (C) 2025 The Android Open Source Project
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
	"archive/zip"
	"errors"
	"io"
	"os"
	"path/filepath"
	"regexp"
	"testing"

	"github.com/google/android-cuttlefish/e2etests/orchestration/common"

	hoclient "github.com/google/android-cuttlefish/frontend/src/libhoclient"
)

func TestBugReport(t *testing.T) {
	ctx, err := common.Setup(61002)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() {
		common.Cleanup(ctx)
	})
	srv := hoclient.NewHostOrchestratorService(ctx.ServiceURL)
	uploadDir, err := srv.CreateUploadDir()
	if err != nil {
		t.Fatal(err)
	}
	if err := common.UploadAndExtract(srv, uploadDir, "../artifacts/images.zip"); err != nil {
		t.Fatal(err)
	}
	if err := common.UploadAndExtract(srv, uploadDir, "../artifacts/cvd-host_package.tar.gz"); err != nil {
		t.Fatal(err)
	}
	cvd, err := common.CreateCVDFromUserArtifactsDir(srv, uploadDir)
	if err != nil {
		t.Fatalf("failed creating instance: %s", err)
	}

	zipFilename, err := createBugReport(srv, cvd.Group)
	if err != nil {
		t.Fatalf("failed creating bugreport: %s", err)
	}

	if err := assertZipIntegrity(zipFilename); err != nil {
		t.Errorf("invalid zip file: %s", err)
	}
	if err := assertAdbBugReportIsIncluded(zipFilename); err != nil {
		t.Errorf("failed adb bugreport check: %s", err)
	}
}

func createBugReport(srv hoclient.HostOrchestratorService, group string) (string, error) {
	dir, err := os.MkdirTemp("", "bugreport_test")
	if err != nil {
		return "", err
	}
	filename := filepath.Join(dir, "host_bugreport.zip")
	f, err := os.Create(filename)
	if err != nil {
		return "", err
	}
	opts := hoclient.CreateBugReportOpts{
		IncludeADBBugReport: true,
	}
	if err := srv.CreateBugReport(group, opts, f); err != nil {
		return "", err
	}
	if err := f.Close(); err != nil {
		return "", err
	}
	return filename, nil
}

// The zip file is verified checking for errors extracting
// each file in the archive.
func assertZipIntegrity(filename string) error {
	r, err := zip.OpenReader(filename)
	if err != nil {
		return err
	}
	defer r.Close()
	for _, f := range r.File {
		rc, err := f.Open()
		if err != nil {
			return err
		}
		_, readErr := io.Copy(io.Discard, rc)
		rc.Close()
		if readErr != nil {
			return readErr
		}
	}
	return nil
}

func assertAdbBugReportIsIncluded(filename string) error {
	r, err := zip.OpenReader(filename)
	if err != nil {
		return err
	}
	defer r.Close()
	re, err := regexp.Compile(`^bugreport-aosp_cf_x86_64_phone-MAIN\..*\.zip$`)
	if err != nil {
		return err
	}
	for _, f := range r.File {
		if re.MatchString(f.FileHeader.Name) {
			return nil
		}
	}
	return errors.New("not found")
}
