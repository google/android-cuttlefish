// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package orchestrator

import (
	"context"
	"errors"
	"fmt"
	"io/ioutil"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/cvd"
	orchtesting "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/testing"
	apiv1 "github.com/google/android-cuttlefish/frontend/src/liboperator/api/v1"
	"github.com/google/android-cuttlefish/frontend/src/liboperator/operator"

	"github.com/google/go-cmp/cmp"
)

func TestCreateCVDInvalidRequestsEmptyFields(t *testing.T) {
	validRequest := func() *apiv1.CreateCVDRequest {
		return &apiv1.CreateCVDRequest{
			CVD: &apiv1.CVD{
				BuildSource: &apiv1.BuildSource{
					AndroidCIBuildSource: &apiv1.AndroidCIBuildSource{
						MainBuild: &apiv1.AndroidCIBuild{
							BuildID: "1234",
							Target:  "aosp_cf_x86_64_phone-trunk_staging-userdebug",
						},
					},
				},
			},
		}
	}
	// Make sure the valid request is indeed valid.
	if err := validateRequest(validRequest()); err != nil {
		t.Fatalf("the valid request is not valid")
	}
	var tests = []struct {
		corruptRequest func(r *apiv1.CreateCVDRequest)
	}{
		{func(r *apiv1.CreateCVDRequest) { r.CVD.BuildSource = nil }},
		{func(r *apiv1.CreateCVDRequest) { r.CVD.BuildSource.AndroidCIBuildSource = nil }},
		{func(r *apiv1.CreateCVDRequest) {
			r.CVD.BuildSource.AndroidCIBuildSource = nil
			r.CVD.BuildSource.UserBuildSource = &apiv1.UserBuildSource{ArtifactsDir: ""}
		}},
	}
	for _, test := range tests {
		req := validRequest()
		test.corruptRequest(req)
		opts := CreateCVDActionOpts{
			Request: req,
		}
		action := NewCreateCVDAction(opts)

		_, err := action.Run()

		var appErr *operator.AppError
		if !errors.As(err, &appErr) {
			t.Errorf("error type <<\"%T\">> not found in error chain", appErr)
		}
		var emptyFieldErr EmptyFieldError
		if !errors.As(err, &emptyFieldErr) {
			t.Errorf("error type <<\"%T\">> not found in error chain", emptyFieldErr)
		}
	}
}

func TestCreateCVDSameTargetArtifactsIsDownloadedOnce(t *testing.T) {
	dir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, dir)
	fetchCVDExecCounter := 0
	execContext := func(ctx context.Context, name string, args ...string) *exec.Cmd {
		if filepath.Base(name) == "fetch_cvd" {
			fetchCVDExecCounter += 1
		}
		return exec.Command("true")
	}
	paths := IMPaths{ArtifactsRootDir: dir + "/artifacts"}
	om := NewMapOM()
	buildAPI := &fakeBuildAPI{}
	artifactsFetcher := newBuildAPIArtifactsFetcher(buildAPI)
	cvdBundleFetcher := newFetchCVDCommandArtifactsFetcher(execContext, "")
	opts := CreateCVDActionOpts{
		Request:          &apiv1.CreateCVDRequest{CVD: &apiv1.CVD{BuildSource: androidCISource("1", "foo")}},
		HostValidator:    &AlwaysSucceedsValidator{},
		Paths:            paths,
		OperationManager: om,
		ExecContext:      execContext,
		BuildAPI:         buildAPI,
		ArtifactsFetcher: artifactsFetcher,
		CVDBundleFetcher: cvdBundleFetcher,
		CVDUser:          fakeCVDUser,
	}
	action := NewCreateCVDAction(opts)

	op1, err := action.Run()
	orchtesting.FatalIfNil(t, err)
	op2, err := action.Run()
	orchtesting.FatalIfNil(t, err)

	_, err = om.Wait(op1.Name, 1*time.Second)
	orchtesting.FatalIfNil(t, err)
	_, err = om.Wait(op2.Name, 1*time.Second)
	orchtesting.FatalIfNil(t, err)
	if fetchCVDExecCounter == 0 {
		t.Error("`fetch_cvd` was never executed")
	}
	if fetchCVDExecCounter > 1 {
		t.Errorf("`fetch_cvd` was executed more than once, it was <<%d>> times", fetchCVDExecCounter)
	}
}

func TestCreateCVDVerifyRootDirectoriesAreCreated(t *testing.T) {
	dir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, dir)
	execContext := execCtxAlwaysSucceeds
	paths := IMPaths{ArtifactsRootDir: dir + "/artifacts"}
	om := NewMapOM()
	buildAPI := &fakeBuildAPI{}
	artifactsFetcher := newBuildAPIArtifactsFetcher(buildAPI)
	cvdBundleFetcher := newFetchCVDCommandArtifactsFetcher(execContext, "")
	opts := CreateCVDActionOpts{
		Request:          &apiv1.CreateCVDRequest{CVD: &apiv1.CVD{BuildSource: androidCISource("1", "foo")}},
		HostValidator:    &AlwaysSucceedsValidator{},
		Paths:            paths,
		OperationManager: om,
		ExecContext:      execContext,
		BuildAPI:         buildAPI,
		ArtifactsFetcher: artifactsFetcher,
		CVDBundleFetcher: cvdBundleFetcher,
		CVDUser:          fakeCVDUser,
	}
	action := NewCreateCVDAction(opts)

	op, err := action.Run()

	orchtesting.FatalIfNil(t, err)
	_, err = om.Wait(op.Name, 1*time.Second)
	orchtesting.FatalIfNil(t, err)
	stats, err := os.Stat(paths.ArtifactsRootDir)
	orchtesting.FatalIfNil(t, err)
	if diff := cmp.Diff("drwxrwxr--", stats.Mode().String()); diff != "" {
		t.Errorf("mode mismatch (-want +got):\n%s", diff)
	}
}

func TestCreateCVDVerifyStartCVDCmdArgs(t *testing.T) {
	dir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, dir)
	goldenPrefixFmt := fmt.Sprintf("sudo -u fakecvduser "+
		"ANDROID_HOST_OUT=%[1]s/artifacts/%%[1]s %[2]s --group_name=cvd start --daemon --report_anonymous_usage_stats=y"+
		" --base_instance_num=1 --system_image_dir=%[1]s/artifacts/%%[1]s", dir, cvd.CVDBin)
	tests := []struct {
		name string
		req  apiv1.CreateCVDRequest
		exp  string
	}{
		{
			name: "android ci build default",
			req: apiv1.CreateCVDRequest{
				CVD: &apiv1.CVD{
					BuildSource: &apiv1.BuildSource{
						AndroidCIBuildSource: &apiv1.AndroidCIBuildSource{},
					},
				},
			},
			exp: fmt.Sprintf(goldenPrefixFmt,
				fmt.Sprintf("%s_%s__cvd", fakeLatesGreenBuildID, mainBuildDefaultTarget)),
		},
		{
			name: "android ci build specific main build",
			req: apiv1.CreateCVDRequest{
				CVD: &apiv1.CVD{
					BuildSource: &apiv1.BuildSource{
						AndroidCIBuildSource: &apiv1.AndroidCIBuildSource{
							MainBuild: &apiv1.AndroidCIBuild{
								BuildID: "1",
								Target:  "foo",
							},
						},
					},
				},
			},
			exp: fmt.Sprintf(goldenPrefixFmt, "1_foo__cvd"),
		},
		{
			name: "android ci build specific kernel build",
			req: apiv1.CreateCVDRequest{
				CVD: &apiv1.CVD{
					BuildSource: &apiv1.BuildSource{
						AndroidCIBuildSource: &apiv1.AndroidCIBuildSource{
							MainBuild: &apiv1.AndroidCIBuild{
								BuildID: "1",
								Target:  "foo",
							},
							KernelBuild: &apiv1.AndroidCIBuild{
								BuildID: "137",
								Target:  "bar",
							},
						},
					},
				},
			},
			exp: fmt.Sprintf(goldenPrefixFmt, "1_foo__cvd") +
				" --kernel_path=" + dir + "/artifacts/137_bar__kernel/bzImage" +
				" --initramfs_path=" + dir + "/artifacts/137_bar__kernel/initramfs.img",
		},
		{
			name: "android ci build specific bootloader build",
			req: apiv1.CreateCVDRequest{
				CVD: &apiv1.CVD{
					BuildSource: &apiv1.BuildSource{
						AndroidCIBuildSource: &apiv1.AndroidCIBuildSource{
							MainBuild: &apiv1.AndroidCIBuild{
								BuildID: "1",
								Target:  "foo",
							},
							BootloaderBuild: &apiv1.AndroidCIBuild{
								BuildID: "137",
								Target:  "bar",
							},
						},
					},
				},
			},
			exp: fmt.Sprintf(goldenPrefixFmt, "1_foo__cvd") +
				" --bootloader=" + dir + "/artifacts/137_bar__bootloader/u-boot.rom",
		},
		{
			name: "android ci build with system image build",
			req: apiv1.CreateCVDRequest{
				CVD: &apiv1.CVD{
					BuildSource: &apiv1.BuildSource{
						AndroidCIBuildSource: &apiv1.AndroidCIBuildSource{
							MainBuild: &apiv1.AndroidCIBuild{
								BuildID: "1",
								Target:  "foo",
							},
							SystemImageBuild: &apiv1.AndroidCIBuild{
								BuildID: "137",
								Target:  "bar",
							},
						},
					},
				},
			},
			exp: fmt.Sprintf(goldenPrefixFmt, fakeUUID+"__custom_cvd"),
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			var usedCmdName string
			var usedCmdArgs []string
			execContext := func(cxt context.Context, name string, args ...string) *exec.Cmd {
				if containsStr(args, "start") {
					usedCmdName = name
					usedCmdArgs = args
				}
				return exec.Command("true")
			}
			paths := IMPaths{ArtifactsRootDir: dir + "/artifacts"}
			om := NewMapOM()
			buildAPI := &fakeBuildAPI{}
			artifactsFetcher := newBuildAPIArtifactsFetcher(buildAPI)
			cvdBundleFetcher := newFetchCVDCommandArtifactsFetcher(execContext, "")
			opts := CreateCVDActionOpts{
				Request:          &tc.req,
				HostValidator:    &AlwaysSucceedsValidator{},
				Paths:            paths,
				OperationManager: om,
				ExecContext:      execContext,
				BuildAPI:         buildAPI,
				ArtifactsFetcher: artifactsFetcher,
				CVDBundleFetcher: cvdBundleFetcher,
				UUIDGen:          fakeUUIDGen,
				CVDUser:          fakeCVDUser,
			}
			action := NewCreateCVDAction(opts)

			op, err := action.Run()

			orchtesting.FatalIfNil(t, err)
			_, err = om.Wait(op.Name, 1*time.Second)
			orchtesting.FatalIfNil(t, err)
			got := usedCmdName + " " + strings.Join(usedCmdArgs, " ")
			if diff := cmp.Diff(tc.exp, got); diff != "" {
				t.Errorf("command line mismatch (-want +got):\n%s", diff)
			}
			// Cleanup
			os.RemoveAll(dir)
			os.MkdirAll(dir, 0774)
		})
	}
}

type fakeUADirRes struct {
	Dir string
}

func (r *fakeUADirRes) GetDirPath(string) string { return r.Dir }

func TestCreateCVDFromUserBuildVerifyStartCVDCmdArgs(t *testing.T) {
	dir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, dir)
	ioutil.WriteFile(dir+"/vbmeta.img", []byte{}, 0755)
	ioutil.WriteFile(dir+"/vbmeta_system.img", []byte{}, 0755)
	expected := fmt.Sprintf("sudo -u fakecvduser "+
		"ANDROID_HOST_OUT=%[1]s %[2]s --group_name=cvd start --daemon --report_anonymous_usage_stats=y"+
		" --base_instance_num=1 --system_image_dir=%[1]s", dir, cvd.CVDBin)
	var usedCmdName string
	var usedCmdArgs []string
	execContext := func(cxt context.Context, name string, args ...string) *exec.Cmd {
		if containsStr(args, "start") {
			usedCmdName = name
			usedCmdArgs = args
		}
		return exec.Command("true")
	}
	paths := IMPaths{ArtifactsRootDir: dir + "/artifacts"}
	om := NewMapOM()
	buildAPI := &fakeBuildAPI{}
	artifactsFetcher := newBuildAPIArtifactsFetcher(buildAPI)
	cvdBundleFetcher := newFetchCVDCommandArtifactsFetcher(execContext, "")
	req := apiv1.CreateCVDRequest{
		CVD: &apiv1.CVD{
			BuildSource: &apiv1.BuildSource{
				UserBuildSource: &apiv1.UserBuildSource{ArtifactsDir: "baz"},
			},
		},
	}
	opts := CreateCVDActionOpts{
		Request:                  &req,
		HostValidator:            &AlwaysSucceedsValidator{},
		Paths:                    paths,
		OperationManager:         om,
		ExecContext:              execContext,
		BuildAPI:                 buildAPI,
		ArtifactsFetcher:         artifactsFetcher,
		CVDBundleFetcher:         cvdBundleFetcher,
		UserArtifactsDirResolver: &fakeUADirRes{dir},
		UUIDGen:                  fakeUUIDGen,
		CVDUser:                  fakeCVDUser,
	}
	action := NewCreateCVDAction(opts)

	op, err := action.Run()

	orchtesting.FatalIfNil(t, err)
	_, err = om.Wait(op.Name, 1*time.Second)
	orchtesting.FatalIfNil(t, err)
	got := usedCmdName + " " + strings.Join(usedCmdArgs, " ")
	if diff := cmp.Diff(expected, got); diff != "" {
		t.Errorf("command line mismatch (-want +got):\n%s", diff)
	}
	// Cleanup
	os.RemoveAll(dir)
	os.MkdirAll(dir, 0774)
}

func TestCreateCVDFailsDueCVDSubCommandExecution(t *testing.T) {
	dir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, dir)
	execContext := execCtxCvdSubcmdFails
	paths := IMPaths{ArtifactsRootDir: dir + "/artifacts"}
	om := NewMapOM()
	buildAPI := &fakeBuildAPI{}
	artifactsFetcher := newBuildAPIArtifactsFetcher(buildAPI)
	cvdBundleFetcher := newFetchCVDCommandArtifactsFetcher(execContext, "")
	opts := CreateCVDActionOpts{
		Request:          &apiv1.CreateCVDRequest{CVD: &apiv1.CVD{BuildSource: androidCISource("1", "foo")}},
		HostValidator:    &AlwaysSucceedsValidator{},
		Paths:            paths,
		OperationManager: om,
		ExecContext:      execContext,
		BuildAPI:         buildAPI,
		ArtifactsFetcher: artifactsFetcher,
		CVDBundleFetcher: cvdBundleFetcher,
		CVDUser:          fakeCVDUser,
	}
	action := NewCreateCVDAction(opts)

	op, err := action.Run()

	orchtesting.FatalIfNil(t, err)
	res, err := om.Wait(op.Name, 1*time.Second)
	orchtesting.FatalIfNil(t, err)
	if res.Error == nil {
		t.Error("expected error")
	}
}

func TestCreateCVDFailsDueTimeout(t *testing.T) {
	dir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, dir)
	execContext := execCtxCvdSubcmdDelays
	paths := IMPaths{ArtifactsRootDir: dir + "/artifacts"}
	om := NewMapOM()
	buildAPI := &fakeBuildAPI{}
	artifactsFetcher := newBuildAPIArtifactsFetcher(buildAPI)
	cvdBundleFetcher := newFetchCVDCommandArtifactsFetcher(execContext, "")
	opts := CreateCVDActionOpts{
		Request:          &apiv1.CreateCVDRequest{CVD: &apiv1.CVD{BuildSource: androidCISource("1", "foo")}},
		HostValidator:    &AlwaysSucceedsValidator{},
		Paths:            paths,
		OperationManager: om,
		ExecContext:      execContext,
		BuildAPI:         buildAPI,
		ArtifactsFetcher: artifactsFetcher,
		CVDBundleFetcher: cvdBundleFetcher,
		CVDStartTimeout:  testFakeBinaryDelayMs - (50 * time.Millisecond),
		CVDUser:          fakeCVDUser,
	}
	action := NewCreateCVDAction(opts)

	op, err := action.Run()

	orchtesting.FatalIfNil(t, err)
	res, err := om.Wait(op.Name, 1*time.Second)
	orchtesting.FatalIfNil(t, err)
	if res.Error == nil {
		t.Error("expected error")
	}
}

type AlwaysFailsValidator struct{}

func (AlwaysFailsValidator) Validate() error {
	return errors.New("validation failed")
}

func TestCreateCVDFailsDueInvalidHost(t *testing.T) {
	dir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, dir)
	execContext := execCtxAlwaysSucceeds
	paths := IMPaths{ArtifactsRootDir: dir + "/artifacts"}
	om := NewMapOM()
	opts := CreateCVDActionOpts{
		Request:          &apiv1.CreateCVDRequest{CVD: &apiv1.CVD{BuildSource: androidCISource("1", "foo")}},
		HostValidator:    &AlwaysFailsValidator{},
		Paths:            paths,
		OperationManager: om,
		ExecContext:      execContext,
		CVDUser:          fakeCVDUser,
	}
	action := NewCreateCVDAction(opts)

	_, err := action.Run()

	if err == nil {
		t.Error("expected error")
	}
}
