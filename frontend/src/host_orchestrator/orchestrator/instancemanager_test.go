// Copyright 2022 Google LLC
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
	"io"
	"io/ioutil"
	"os"
	"os/exec"
	"path"
	"path/filepath"
	"strings"
	"testing"
	"time"

	orchtesting "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/testing"
	apiv1 "github.com/google/android-cuttlefish/frontend/src/liboperator/api/v1"
	"github.com/google/android-cuttlefish/frontend/src/liboperator/operator"

	"github.com/google/go-cmp/cmp"
)

type AlwaysSucceedsValidator struct{}

func (AlwaysSucceedsValidator) Validate() error {
	return nil
}

const fakeLatesGreenBuildID = "9551522"

type fakeBuildAPI struct{}

func (fakeBuildAPI) GetLatestGreenBuildID(string, string) (string, error) {
	return fakeLatesGreenBuildID, nil
}

func (fakeBuildAPI) DownloadArtifact(string, string, string, io.Writer) error {
	return nil
}

const fakeProductName = "aosp_foo_x86_64"

func (fakeBuildAPI) ProductName(buildID, target string) (string, error) {
	return fakeProductName, nil
}

const fakeUUID = "123e4567-"

var fakeUUIDGen = func() string { return fakeUUID }

func TestCreateCVDInvalidRequestsEmptyFields(t *testing.T) {
	im := &CVDToolInstanceManager{}
	validRequest := func() *apiv1.CreateCVDRequest {
		return &apiv1.CreateCVDRequest{
			CVD: &apiv1.CVD{
				BuildSource: &apiv1.BuildSource{
					AndroidCIBuildSource: &apiv1.AndroidCIBuildSource{
						MainBuild: &apiv1.AndroidCIBuild{
							BuildID: "1234",
							Target:  "aosp_cf_x86_64_phone-userdebug",
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
		_, err := im.CreateCVD(*req)
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
	cvdBinAB := AndroidBuild{ID: "1", Target: "xyzzy"}
	paths := IMPaths{
		CVDToolsDir:      dir,
		ArtifactsRootDir: dir + "/artifacts",
		RuntimesRootDir:  dir + "/runtimes",
	}
	om := NewMapOM()
	im := newCVDToolIm(execContext, cvdBinAB, paths, om)
	r1 := apiv1.CreateCVDRequest{CVD: &apiv1.CVD{BuildSource: androidCISource("1", "foo")}}
	r2 := apiv1.CreateCVDRequest{CVD: &apiv1.CVD{BuildSource: androidCISource("1", "foo")}}

	op1, _ := im.CreateCVD(r1)
	op2, _ := im.CreateCVD(r2)

	om.Wait(op1.Name, 1*time.Second)
	om.Wait(op2.Name, 1*time.Second)

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
	cvdBinAB := AndroidBuild{ID: "1", Target: "xyzzy"}
	paths := IMPaths{
		CVDToolsDir:      dir,
		ArtifactsRootDir: dir + "/artifacts",
		RuntimesRootDir:  dir + "/runtimes",
	}
	om := NewMapOM()
	im := newCVDToolIm(execContext, cvdBinAB, paths, om)
	r := apiv1.CreateCVDRequest{CVD: &apiv1.CVD{BuildSource: androidCISource("1", "foo")}}

	op, _ := im.CreateCVD(r)

	om.Wait(op.Name, 1*time.Second)

	stats, _ := os.Stat(paths.ArtifactsRootDir)
	if diff := cmp.Diff("drwxrwxr--", stats.Mode().String()); diff != "" {
		t.Errorf("mode mismatch (-want +got):\n%s", diff)
	}
	stats, _ = os.Stat(paths.RuntimesRootDir)
	if diff := cmp.Diff("dgrwxrwxr--", stats.Mode().String()); diff != "" {
		t.Errorf("mode mismatch (-want +got):\n%s", diff)
	}
}

func TestCreateCVDVerifyStartCVDCmdArgs(t *testing.T) {
	dir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, dir)
	goldenPrefixFmt := fmt.Sprintf("sudo -u _cvd-executor HOME=%[1]s/runtimes "+
		"ANDROID_HOST_OUT=%[1]s/artifacts/%%[1]s "+"%[1]s/cvd --group_name=cvd start --daemon --report_anonymous_usage_stats=y"+
		" --base_instance_num=1 --system_image_dir=%[1]s/artifacts/%%[1]s", dir)
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
			om := NewMapOM()
			opts := CVDToolInstanceManagerOpts{
				ExecContext:     execContext,
				CVDToolsVersion: AndroidBuild{ID: "1", Target: "xyzzy"},
				Paths: IMPaths{
					CVDToolsDir:      dir,
					ArtifactsRootDir: dir + "/artifacts",
					RuntimesRootDir:  dir + "/runtimes",
				},
				OperationManager: om,
				HostValidator:    &AlwaysSucceedsValidator{},
				BuildAPIFactory:  func(_ string) BuildAPI { return &fakeBuildAPI{} },
				UUIDGen:          fakeUUIDGen,
			}
			im := NewCVDToolInstanceManager(&opts)

			op, err := im.CreateCVD(tc.req)

			if err != nil {
				t.Fatal(err)
			}
			om.Wait(op.Name, 1*time.Second)
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
	tarContent, _ := ioutil.ReadFile(getTestTarFilename())
	ioutil.WriteFile(dir+"/"+CVDHostPackageName, tarContent, 0755)
	expected := fmt.Sprintf("sudo -u _cvd-executor HOME=%[1]s/runtimes "+
		"ANDROID_HOST_OUT=%[1]s "+"%[1]s/cvd --group_name=cvd start --daemon --report_anonymous_usage_stats=y"+
		" --base_instance_num=1 --system_image_dir=%[1]s", dir)
	var usedCmdName string
	var usedCmdArgs []string
	execContext := func(cxt context.Context, name string, args ...string) *exec.Cmd {
		if containsStr(args, "start") {
			usedCmdName = name
			usedCmdArgs = args
		}
		return exec.Command("true")
	}
	om := NewMapOM()
	opts := CVDToolInstanceManagerOpts{
		ExecContext:     execContext,
		CVDToolsVersion: AndroidBuild{ID: "1", Target: "xyzzy"},
		Paths: IMPaths{
			CVDToolsDir:      dir,
			ArtifactsRootDir: dir + "/artifacts",
			RuntimesRootDir:  dir + "/runtimes",
		},
		OperationManager:         om,
		HostValidator:            &AlwaysSucceedsValidator{},
		UserArtifactsDirResolver: &fakeUADirRes{dir},
		BuildAPIFactory:          func(_ string) BuildAPI { return &fakeBuildAPI{} },
	}
	im := NewCVDToolInstanceManager(&opts)
	req := apiv1.CreateCVDRequest{
		CVD: &apiv1.CVD{
			BuildSource: &apiv1.BuildSource{
				UserBuildSource: &apiv1.UserBuildSource{ArtifactsDir: "baz"},
			},
		},
	}

	op, err := im.CreateCVD(req)

	if err != nil {
		t.Fatal(err)
	}
	om.Wait(op.Name, 1*time.Second)
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
	cvdBinAB := AndroidBuild{ID: "1", Target: "xyzzy"}
	paths := IMPaths{
		CVDToolsDir:      dir,
		ArtifactsRootDir: dir + "/artifacts",
		RuntimesRootDir:  dir + "/runtimes",
	}
	om := NewMapOM()
	im := newCVDToolIm(execContext, cvdBinAB, paths, om)
	r := apiv1.CreateCVDRequest{CVD: &apiv1.CVD{BuildSource: androidCISource("1", "foo")}}

	op, _ := im.CreateCVD(r)

	res, _ := om.Wait(op.Name, 1*time.Second)
	if res.Error == nil {
		t.Error("expected error")
	}
}

func TestCreateCVDFailsDueTimeout(t *testing.T) {
	dir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, dir)
	execContext := execCtxCvdSubcmdDelays
	cvdBinAB := AndroidBuild{ID: "1", Target: "xyzzy"}
	paths := IMPaths{
		CVDToolsDir:      dir,
		ArtifactsRootDir: dir + "/artifacts",
		RuntimesRootDir:  dir + "/runtimes",
	}
	om := NewMapOM()
	opts := CVDToolInstanceManagerOpts{
		ExecContext:      execContext,
		CVDToolsVersion:  cvdBinAB,
		Paths:            paths,
		OperationManager: om,
		CVDStartTimeout:  testFakeBinaryDelayMs - (50 * time.Millisecond),
		HostValidator:    &AlwaysSucceedsValidator{},
		BuildAPIFactory:  func(_ string) BuildAPI { return &fakeBuildAPI{} },
	}
	im := NewCVDToolInstanceManager(&opts)
	r := apiv1.CreateCVDRequest{CVD: &apiv1.CVD{BuildSource: androidCISource("1", "foo")}}

	op, _ := im.CreateCVD(r)

	res, _ := om.Wait(op.Name, 1*time.Second)
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
	cvdBinAB := AndroidBuild{ID: "1", Target: "xyzzy"}
	paths := IMPaths{
		CVDToolsDir:      dir,
		ArtifactsRootDir: dir + "/artifacts",
		RuntimesRootDir:  dir + "/runtimes",
	}
	om := NewMapOM()
	opts := CVDToolInstanceManagerOpts{
		ExecContext:      execContext,
		CVDToolsVersion:  cvdBinAB,
		Paths:            paths,
		OperationManager: om,
		HostValidator:    &AlwaysFailsValidator{},
		BuildAPIFactory:  func(_ string) BuildAPI { return &fakeBuildAPI{} },
	}
	im := NewCVDToolInstanceManager(&opts)
	r := apiv1.CreateCVDRequest{CVD: &apiv1.CVD{BuildSource: androidCISource("1", "foo")}}

	_, err := im.CreateCVD(r)

	if err == nil {
		t.Error("expected error")
	}

}

func TestListCVDsSucceeds(t *testing.T) {
	dir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, dir)
	output := `[
  [
          {
                  "adb_serial" : "0.0.0.0:6520",
                  "assembly_dir" : "/var/lib/cuttlefish-common/runtimes/cvd-1/cuttlefish/assembly",
                  "displays" :
                  [
                          "720 x 1280 ( 320 )"
                  ],
                  "instance_dir" : "/var/lib/cuttlefish-common/runtimes/cvd-1/cuttlefish/instances/cvd-1",
                  "instance_name" : "cvd-1",
                  "status" : "Running",
                  "web_access" : "https:///run/cuttlefish/operator:8443/client.html?deviceId=cvd-1",
                  "webrtc_port" : "8443"
          }
  ]
]`
	execContext := func(ctx context.Context, name string, args ...string) *exec.Cmd {
		cmd := exec.Command("true")
		if path.Base(args[len(args)-1]) == "fleet" {
			// Prints to stderr as well to make sure only stdout is used.
			cmd = exec.Command("tee", "/dev/stderr")
			cmd.Stdin = strings.NewReader(strings.TrimSpace(output))
		}
		return cmd
	}
	cvdBinAB := AndroidBuild{ID: "1", Target: "xyzzy"}
	paths := IMPaths{
		CVDToolsDir:      dir,
		ArtifactsRootDir: dir + "/artifacts",
		RuntimesRootDir:  dir + "/runtimes",
	}
	om := NewMapOM()
	im := newCVDToolIm(execContext, cvdBinAB, paths, om)

	res, _ := im.ListCVDs()

	want := &apiv1.ListCVDsResponse{CVDs: []*apiv1.CVD{
		{
			Name:        "cvd-1",
			BuildSource: &apiv1.BuildSource{},
			Status:      "Running",
			Displays:    []string{"720 x 1280 ( 320 )"},
		},
	}}
	if diff := cmp.Diff(want, res); diff != "" {
		t.Errorf("response mismatch (-want +got):\n%s", diff)
	}
}

func TestSliceItoa(t *testing.T) {
	tests := []struct {
		in  []uint32
		out []string
	}{
		{
			in:  []uint32{},
			out: []string{},
		},
		{
			in:  []uint32{79, 83, 89, 97},
			out: []string{"79", "83", "89", "97"},
		},
	}
	for _, tc := range tests {

		res := SliceItoa(tc.in)

		if diff := cmp.Diff(tc.out, res); diff != "" {
			t.Errorf("result mismatch (-want +got):\n%s", diff)
		}
	}

}

type FakeArtifactDownloader struct {
	t       *testing.T
	content string
}

func (d *FakeArtifactDownloader) Download(dst io.Writer, _ AndroidBuild, name string) error {
	r := strings.NewReader(d.content)
	if _, err := io.Copy(dst, r); err != nil {
		d.t.Fatal(err)
	}
	return nil
}

// Helper constructor
func newCVDToolIm(execContext ExecContext,
	cvdBinAB AndroidBuild,
	paths IMPaths,
	om OperationManager) *CVDToolInstanceManager {
	opts := CVDToolInstanceManagerOpts{
		ExecContext:      execContext,
		CVDToolsVersion:  cvdBinAB,
		Paths:            paths,
		OperationManager: om,
		HostValidator:    &AlwaysSucceedsValidator{},
		BuildAPIFactory:  func(_ string) BuildAPI { return &fakeBuildAPI{} },
	}
	return NewCVDToolInstanceManager(&opts)
}

func execCtxAlwaysSucceeds(ctx context.Context, name string, args ...string) *exec.Cmd {
	return exec.Command("true")
}

func isCvdSubCommand(name string, args ...string) bool {
	// All cvd executions are run through `sudo`.
	if name != "sudo" {
		return false
	}
	// cvd alone not a cvd subcommand
	if path.Base(args[len(args)-1]) == "cvd" {
		return false
	}
	return true
}

func execCtxCvdSubcmdFails(ctx context.Context, name string, args ...string) *exec.Cmd {
	if isCvdSubCommand(name, args...) {
		return exec.Command("false")
	}
	return exec.Command("true")
}

const testFakeBinaryDelayMs = 100 * time.Millisecond

func execCtxCvdSubcmdDelays(ctx context.Context, name string, args ...string) *exec.Cmd {
	if isCvdSubCommand(name, args...) {
		return exec.Command(fmt.Sprintf("sleep %f", float64(testFakeBinaryDelayMs)/1000_000_000))
	}
	return exec.Command("true")
}

func containsStr(values []string, t string) bool {
	for _, v := range values {
		if v == t {
			return true
		}
	}
	return false
}

func androidCISource(buildID, target string) *apiv1.BuildSource {
	return &apiv1.BuildSource{
		AndroidCIBuildSource: &apiv1.AndroidCIBuildSource{
			MainBuild: &apiv1.AndroidCIBuild{
				BuildID: buildID,
				Target:  target,
			},
		},
	}
}
