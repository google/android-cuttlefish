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
	"errors"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"os/exec"
	"path"
	"reflect"
	"strings"
	"testing"
	"time"

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
	dir := tempDir(t)
	defer removeDir(t, dir)
	fetchCVDExecCounter := 0
	execContext := func(name string, args ...string) *exec.Cmd {
		if contains(args, "fetch") {
			fetchCVDExecCounter += 1
		}
		return exec.Command("true")
	}
	cvdBinAB := AndroidBuild{ID: "1", Target: "xyzzy"}
	paths := IMPaths{
		CVDBin:           dir + "/cvd",
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
		t.Error("`cvd fetch` was never executed")
	}
	if fetchCVDExecCounter > 1 {
		t.Errorf("`cvd fetch` was downloaded more than once, it was <<%d>> times", fetchCVDExecCounter)
	}
}

func TestCreateCVDInstanceRuntimeDirAlreadyExist(t *testing.T) {
	dir := tempDir(t)
	defer removeDir(t, dir)
	execContext := execCtxAlwaysSucceeds
	cvdBinAB := AndroidBuild{ID: "1", Target: "xyzzy"}
	paths := IMPaths{
		CVDBin:           dir + "/cvd",
		ArtifactsRootDir: dir + "/artifacts",
		RuntimesRootDir:  dir + "/runtimes",
	}
	om := NewMapOM()
	im1 := newCVDToolIm(execContext, cvdBinAB, paths, om)
	r := apiv1.CreateCVDRequest{CVD: &apiv1.CVD{BuildSource: androidCISource("1", "foo")}}
	op, _ := im1.CreateCVD(r)
	om.Wait(op.Name, 1*time.Second)
	// The second instance manager is created with the same im paths as the previous instance
	// manager, this will lead to create an instance runtime dir that already exist.
	im2 := newCVDToolIm(execContext, cvdBinAB, paths, om)

	op, _ = im2.CreateCVD(r)

	res, _ := om.Wait(op.Name, 1*time.Second)
	if res.Error == nil {
		t.Error("expected error due instance runtime dir already existing")
	}
}

func TestCreateCVDVerifyRootDirectoriesAreCreated(t *testing.T) {
	dir := tempDir(t)
	defer removeDir(t, dir)
	execContext := execCtxAlwaysSucceeds
	cvdBinAB := AndroidBuild{ID: "1", Target: "xyzzy"}
	paths := IMPaths{
		CVDBin:           dir + "/cvd",
		ArtifactsRootDir: dir + "/artifacts",
		RuntimesRootDir:  dir + "/runtimes",
	}
	om := NewMapOM()
	im := newCVDToolIm(execContext, cvdBinAB, paths, om)
	r := apiv1.CreateCVDRequest{CVD: &apiv1.CVD{BuildSource: androidCISource("1", "foo")}}

	op, _ := im.CreateCVD(r)

	om.Wait(op.Name, 1*time.Second)

	expected := "drwxrwxr--"
	stats, _ := os.Stat(paths.ArtifactsRootDir)
	if stats.Mode().String() != expected {
		t.Errorf("expected <<%q>, got %q", expected, stats.Mode().String())
	}
	stats, _ = os.Stat(paths.RuntimesRootDir)
	if stats.Mode().String() != expected {
		t.Errorf("expected <<%q>, got %q", expected, stats.Mode().String())
	}
}

func TestCreateCVDVerifyFetchCVDCmdArgs(t *testing.T) {
	dir := tempDir(t)
	defer removeDir(t, dir)
	var usedCmdName string
	var usedCmdArgs []string
	execContext := func(name string, args ...string) *exec.Cmd {
		if contains(args, "fetch") {
			usedCmdName = name
			usedCmdArgs = args
		}
		return exec.Command("true")
	}
	cvdBinAB := AndroidBuild{ID: "1", Target: "xyzzy"}
	paths := IMPaths{
		CVDBin:           dir + "/cvd",
		ArtifactsRootDir: dir + "/artifacts",
		RuntimesRootDir:  dir + "/runtimes",
	}
	om := NewMapOM()
	im := newCVDToolIm(execContext, cvdBinAB, paths, om)
	r := apiv1.CreateCVDRequest{CVD: &apiv1.CVD{BuildSource: androidCISource("1", "foo")}}

	op, _ := im.CreateCVD(r)

	om.Wait(op.Name, 1*time.Second)
	if usedCmdName != "sudo" {
		t.Errorf("expected 'sudo', got %q", usedCmdName)
	}
	expectedCmdArgs := []string{
		"-u", "_cvd-executor", envVarAndroidHostOut + "=", envVarHome + "=", paths.CVDBin, "fetch",
		"--default_build=1/foo", "--directory=" + paths.ArtifactsRootDir + "/1_foo__cvd",
	}
	if !reflect.DeepEqual(usedCmdArgs, expectedCmdArgs) {
		t.Errorf("invalid args\nexpected: %+v\ngot:      %+v", expectedCmdArgs, usedCmdArgs)
	}
}

func TestCreateCVDVerifyStartCVDCmdArgs(t *testing.T) {
	dir := tempDir(t)
	defer removeDir(t, dir)
	var usedCmdName string
	var usedCmdArgs []string
	execContext := func(name string, args ...string) *exec.Cmd {
		if contains(args, "start") {
			usedCmdName = name
			usedCmdArgs = args
		}
		return exec.Command("true")
	}
	cvdBinAB := AndroidBuild{ID: "1", Target: "xyzzy"}
	paths := IMPaths{
		CVDBin:           dir + "/cvd",
		ArtifactsRootDir: dir + "/artifacts",
		RuntimesRootDir:  dir + "/runtimes",
	}
	om := NewMapOM()
	im := newCVDToolIm(execContext, cvdBinAB, paths, om)
	r := apiv1.CreateCVDRequest{CVD: &apiv1.CVD{BuildSource: androidCISource("1", "foo")}}

	op, _ := im.CreateCVD(r)

	om.Wait(op.Name, 1*time.Second)
	if usedCmdName != "sudo" {
		t.Errorf("expected 'sudo', got %q", usedCmdName)
	}
	artifactsDir := paths.ArtifactsRootDir + "/1_foo__cvd"
	runtimeDir := paths.RuntimesRootDir + "/cvd-1"
	expectedCmdArgs := []string{
		"-u", "_cvd-executor", envVarAndroidHostOut + "=" + artifactsDir, envVarHome + "=" + runtimeDir,
		paths.CVDBin, "start", daemonArg, reportAnonymousUsageStatsArg,
		"--base_instance_num=1", "--system_image_dir=" + artifactsDir,
	}
	if !reflect.DeepEqual(usedCmdArgs, expectedCmdArgs) {
		t.Errorf("invalid args\nexpected: %+v\ngot:      %+v", expectedCmdArgs, usedCmdArgs)
	}
}

func TestCreateCVDWithSpecificKernelBuildVerifyStartCVDCmdArgs(t *testing.T) {
	dir := tempDir(t)
	defer removeDir(t, dir)
	var usedCmdName string
	var usedCmdArgs []string
	execContext := func(name string, args ...string) *exec.Cmd {
		if contains(args, "start") {
			usedCmdName = name
			usedCmdArgs = args
		}
		return exec.Command("true")
	}
	cvdBinAB := AndroidBuild{ID: "1", Target: "xyzzy"}
	paths := IMPaths{
		CVDBin:           dir + "/cvd",
		ArtifactsRootDir: dir + "/artifacts",
		RuntimesRootDir:  dir + "/runtimes",
	}
	om := NewMapOM()
	im := newCVDToolIm(execContext, cvdBinAB, paths, om)
	r := apiv1.CreateCVDRequest{
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
	}

	op, _ := im.CreateCVD(r)

	om.Wait(op.Name, 1*time.Second)
	wantFmt := "sudo -u _cvd-executor ANDROID_HOST_OUT=/tmp/%[1]s/artifacts/1_foo__cvd" +
		" HOME=/tmp/%[1]s/runtimes/cvd-1 /tmp/%[1]s/cvd start --daemon --report_anonymous_usage_stats=y" +
		" --base_instance_num=1 --system_image_dir=/tmp/%[1]s/artifacts/1_foo__cvd" +
		" --kernel_path=/tmp/%[1]s/artifacts/137_bar__kernel/bzImage"
	want := fmt.Sprintf(wantFmt, path.Base(dir))
	got := usedCmdName + " " + strings.Join(usedCmdArgs, " ")
	if diff := cmp.Diff(want, got); diff != "" {
		t.Errorf("command line mismatch (-want +got):\n%s", diff)
	}
}

func TestCreateCVDSucceeds(t *testing.T) {
	dir := tempDir(t)
	defer removeDir(t, dir)
	execContext := execCtxAlwaysSucceeds
	cvdBinAB := AndroidBuild{ID: "1", Target: "xyzzy"}
	paths := IMPaths{
		CVDBin:           dir + "/cvd",
		ArtifactsRootDir: dir + "/artifacts",
		RuntimesRootDir:  dir + "/runtimes",
	}
	om := NewMapOM()
	im := newCVDToolIm(execContext, cvdBinAB, paths, om)
	buildSource := androidCISource("1", "foo")
	r := apiv1.CreateCVDRequest{CVD: &apiv1.CVD{BuildSource: buildSource}}

	op, _ := im.CreateCVD(r)

	res, _ := om.Wait(op.Name, 1*time.Second)
	want := &apiv1.CVD{Name: "cvd-1", BuildSource: buildSource}
	if diff := cmp.Diff(want, res.Value); diff != "" {
		t.Errorf("cvd mismatch (-want +got):\n%s", diff)
	}
}

func TestCreateCVDLatesGreenSucceeds(t *testing.T) {
	dir := tempDir(t)
	defer removeDir(t, dir)
	execContext := execCtxAlwaysSucceeds
	cvdBinAB := AndroidBuild{ID: "1", Target: "xyzzy"}
	paths := IMPaths{
		CVDBin:           dir + "/cvd",
		ArtifactsRootDir: dir + "/artifacts",
		RuntimesRootDir:  dir + "/runtimes",
	}
	om := NewMapOM()
	opts := CVDToolInstanceManagerOpts{
		ExecContext:      execContext,
		CVDBinAB:         cvdBinAB,
		Paths:            paths,
		OperationManager: om,
		HostValidator:    &AlwaysSucceedsValidator{},
		BuildAPI:         &fakeBuildAPI{},
	}
	im := NewCVDToolInstanceManager(&opts)
	r := apiv1.CreateCVDRequest{
		CVD: &apiv1.CVD{
			BuildSource: &apiv1.BuildSource{
				AndroidCIBuildSource: &apiv1.AndroidCIBuildSource{},
			},
		},
	}

	op, _ := im.CreateCVD(r)

	res, _ := om.Wait(op.Name, 1*time.Second)
	want := &apiv1.CVD{
		Name: "cvd-1",
		BuildSource: &apiv1.BuildSource{
			AndroidCIBuildSource: &apiv1.AndroidCIBuildSource{},
		},
	}
	if diff := cmp.Diff(want, res.Value); diff != "" {
		t.Errorf("cvd mismatch (-want +got):\n%s", diff)
	}
}

type fakeUADirRes struct {
	Dir string
}

func (r *fakeUADirRes) GetDirPath(string) string { return r.Dir }

func TestCreateCVDWithUserBuildSucceeds(t *testing.T) {
	dir := tempDir(t)
	defer removeDir(t, dir)
	tarContent, _ := ioutil.ReadFile(getTestTarFilename())
	ioutil.WriteFile(dir+"/"+CVDHostPackageName, tarContent, 0755)
	execContext := execCtxAlwaysSucceeds
	cvdBinAB := AndroidBuild{ID: "1", Target: "xyzzy"}
	paths := IMPaths{
		CVDBin:           dir + "/cvd",
		ArtifactsRootDir: dir + "/artifacts",
		RuntimesRootDir:  dir + "/runtimes",
	}
	om := NewMapOM()
	opts := CVDToolInstanceManagerOpts{
		ExecContext:              execContext,
		CVDBinAB:                 cvdBinAB,
		Paths:                    paths,
		OperationManager:         om,
		HostValidator:            &AlwaysSucceedsValidator{},
		UserArtifactsDirResolver: &fakeUADirRes{dir},
		BuildAPI:                 &fakeBuildAPI{},
	}
	im := NewCVDToolInstanceManager(&opts)
	buildSource := &apiv1.BuildSource{UserBuildSource: &apiv1.UserBuildSource{ArtifactsDir: "baz"}}
	r := apiv1.CreateCVDRequest{CVD: &apiv1.CVD{BuildSource: buildSource}}

	op, _ := im.CreateCVD(r)

	res, _ := om.Wait(op.Name, 1*time.Second)
	want := &apiv1.CVD{Name: "cvd-1", BuildSource: buildSource}
	if diff := cmp.Diff(want, res.Value); diff != "" {
		t.Errorf("cvd mismatch (-want +got):\n%s", diff)
	}
}

func TestCreateCVDFailsDueCVDSubCommandExecution(t *testing.T) {
	dir := tempDir(t)
	defer removeDir(t, dir)
	execContext := execCtxSubcmdFails
	cvdBinAB := AndroidBuild{ID: "1", Target: "xyzzy"}
	paths := IMPaths{
		CVDBin:           dir + "/cvd",
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
	dir := tempDir(t)
	defer removeDir(t, dir)
	execContext := execCtxSubcmdDelays
	cvdBinAB := AndroidBuild{ID: "1", Target: "xyzzy"}
	paths := IMPaths{
		CVDBin:           dir + "/cvd",
		ArtifactsRootDir: dir + "/artifacts",
		RuntimesRootDir:  dir + "/runtimes",
	}
	om := NewMapOM()
	opts := CVDToolInstanceManagerOpts{
		ExecContext:      execContext,
		CVDBinAB:         cvdBinAB,
		Paths:            paths,
		OperationManager: om,
		CVDExecTimeout:   testFakeBinaryDelayMs - (50 * time.Millisecond),
		HostValidator:    &AlwaysSucceedsValidator{},
		BuildAPI:         &fakeBuildAPI{},
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
	dir := tempDir(t)
	defer removeDir(t, dir)
	execContext := execCtxAlwaysSucceeds
	cvdBinAB := AndroidBuild{ID: "1", Target: "xyzzy"}
	paths := IMPaths{
		CVDBin:           dir + "/cvd",
		ArtifactsRootDir: dir + "/artifacts",
		RuntimesRootDir:  dir + "/runtimes",
	}
	om := NewMapOM()
	opts := CVDToolInstanceManagerOpts{
		ExecContext:      execContext,
		CVDBinAB:         cvdBinAB,
		Paths:            paths,
		OperationManager: om,
		HostValidator:    &AlwaysFailsValidator{},
	}
	im := NewCVDToolInstanceManager(&opts)
	r := apiv1.CreateCVDRequest{CVD: &apiv1.CVD{BuildSource: androidCISource("1", "foo")}}

	_, err := im.CreateCVD(r)

	if err == nil {
		t.Error("expected error")
	}

}

func TestListCVDsSucceeds(t *testing.T) {
	dir := tempDir(t)
	defer removeDir(t, dir)
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
	execContext := func(name string, args ...string) *exec.Cmd {
		cmd := exec.Command("true")
		if path.Base(args[len(args)-1]) == "fleet" {
			cmd = exec.Command("echo", strings.TrimSpace(output))
		}
		return cmd
	}
	cvdBinAB := AndroidBuild{ID: "1", Target: "xyzzy"}
	paths := IMPaths{
		CVDBin:           dir + "/cvd",
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

func TestGetLogsDir(t *testing.T) {
	paths := IMPaths{RuntimesRootDir: "/runtimes"}
	im := NewCVDToolInstanceManager(&CVDToolInstanceManagerOpts{Paths: paths})

	got := im.GetLogsDir("cvd-1")

	if diff := cmp.Diff("/runtimes/cvd-1/cuttlefish_runtime/logs", got); diff != "" {
		t.Errorf("cvd mismatch (-want +got):\n%s", diff)
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
		CVDBinAB:         cvdBinAB,
		Paths:            paths,
		OperationManager: om,
		HostValidator:    &AlwaysSucceedsValidator{},
		BuildAPI:         &fakeBuildAPI{},
	}
	return NewCVDToolInstanceManager(&opts)
}

// Creates a temporary directory for the test to use returning its path.
// Each subsequent call creates a unique directory; if the directory creation
// fails, `tempDir` terminates the test by calling Fatal.
func tempDir(t *testing.T) string {
	name, err := ioutil.TempDir("", "cuttlefishTestDir")
	if err != nil {
		t.Fatal(err)
	}
	return name
}

// Removes the directory at the passed path.
// If deletion fails, `removeDir` terminates the test by calling Fatal.
func removeDir(t *testing.T, name string) {
	if err := os.RemoveAll(name); err != nil {
		t.Fatal(err)
	}
}

func execCtxAlwaysSucceeds(name string, args ...string) *exec.Cmd {
	return exec.Command("true")
}

func execCtxSubcmdFails(name string, args ...string) *exec.Cmd {
	cmd := "false"
	// Do not fail when executing cvd only as it is not a cvd subcommand.
	if path.Base(args[len(args)-1]) == "cvd" {
		cmd = "true"
	}
	return exec.Command(cmd)
}

const testFakeBinaryDelayMs = 100 * time.Millisecond

func execCtxSubcmdDelays(name string, args ...string) *exec.Cmd {
	cmd := fmt.Sprintf("sleep %f", float64(testFakeBinaryDelayMs)/1000_000_000)
	// Do not wait when executing cvd only as it is not a cvd subcommand.
	if path.Base(args[len(args)-1]) == "cvd" {
		cmd = "true"
	}
	return exec.Command(cmd)
}

func contains(values []string, t string) bool {
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
