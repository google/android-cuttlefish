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
	"bytes"
	"errors"
	"fmt"
	"io"
	"io/ioutil"
	"net/http"
	"os"
	"os/exec"
	"reflect"
	"runtime"
	"strings"
	"testing"

	apiv1 "cuttlefish/liboperator/api/v1"
	"cuttlefish/liboperator/operator"
)

func TestCreateCVDInvalidRequestsEmptyFields(t *testing.T) {
	im := &InstanceManager{}
	var validRequest = apiv1.CreateCVDRequest{
		BuildInfo: &apiv1.BuildInfo{
			BuildID: "1234",
			Target:  "aosp_cf_x86_64_phone-userdebug",
		},
	}
	// Make sure the valid request is indeed valid.
	if err := validateRequest(&validRequest); err != nil {
		t.Fatalf("the valid request is not valid")
	}
	var tests = []struct {
		corruptRequest func(r *apiv1.CreateCVDRequest)
	}{
		{func(r *apiv1.CreateCVDRequest) { r.BuildInfo = nil }},
		{func(r *apiv1.CreateCVDRequest) { r.BuildInfo.BuildID = "" }},
		{func(r *apiv1.CreateCVDRequest) { r.BuildInfo.Target = "" }},
	}

	for _, test := range tests {
		req := validRequest
		test.corruptRequest(&req)
		_, err := im.CreateCVD(req)
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

type testProcedureStage struct {
	err error
}

func (s *testProcedureStage) Run() error {
	return s.err
}

type testLaunchCVDProcedureBuilder struct {
	err error
}

func (b *testLaunchCVDProcedureBuilder) Build(_ interface{}) Procedure {
	return []ProcedureStage{
		&testProcedureStage{
			err: b.err,
		},
	}
}

func TestCreateCVDLaunchCVDProcedureFails(t *testing.T) {
	om := NewMapOM()
	im := InstanceManager{
		OM:                        om,
		LaunchCVDProcedureBuilder: &testLaunchCVDProcedureBuilder{err: errors.New("error")},
	}
	req := apiv1.CreateCVDRequest{
		BuildInfo: &apiv1.BuildInfo{
			BuildID: "1234",
			Target:  "aosp_cf_x86_64_phone-userdebug",
		},
	}

	op, _ := im.CreateCVD(req)

	op, _ = om.Wait(op.Name)
	if !op.Done {
		t.Error("expected operation to be done")
	}
	if op.Result.Error.ErrorMsg != ErrMsgLaunchCVDFailed {
		t.Errorf("expected <<%q>>, got %q", ErrMsgLaunchCVDFailed, op.Result.Error.ErrorMsg)
	}
}

func TestCreateCVD(t *testing.T) {
	om := NewMapOM()
	im := InstanceManager{
		OM:                        om,
		LaunchCVDProcedureBuilder: &testLaunchCVDProcedureBuilder{},
	}
	req := apiv1.CreateCVDRequest{
		BuildInfo: &apiv1.BuildInfo{
			BuildID: "1234",
			Target:  "aosp_cf_x86_64_phone-userdebug",
		},
	}

	op, _ := im.CreateCVD(req)

	op, _ = om.Wait(op.Name)
	if !op.Done {
		t.Error("expected operation to be done")
	}
	if (op.Result != OperationResult{}) {
		t.Errorf("expected empty result, got %+v", op.Result)
	}
}

type testCounterProcedureStage struct {
	count *int
	err   error
}

func (s *testCounterProcedureStage) Run() error {
	*s.count++
	return s.err
}

func TestProcedureExecuteOneStage(t *testing.T) {
	count := 0
	p := Procedure{&testCounterProcedureStage{count: &count}}

	err := p.Execute()

	if err != nil {
		t.Errorf("epected <<nil>> error, got %#v", err)
	}
	if count != 1 {
		t.Errorf("epected <<1>> error, got %#v", count)
	}
}

func TestProcedureExecuteThreeStages(t *testing.T) {
	count := 0
	p := Procedure{
		&testCounterProcedureStage{count: &count},
		&testCounterProcedureStage{count: &count},
		&testCounterProcedureStage{count: &count},
	}

	err := p.Execute()

	if err != nil {
		t.Errorf("epected <<nil>> error, got %#v", err)
	}
	if count != 3 {
		t.Errorf("epected <<3>> error, got %#v", count)
	}
}

func TestProcedureExecuteInnerStageWithFails(t *testing.T) {
	count := 0
	expectedErr := errors.New("error")
	p := Procedure{
		&testCounterProcedureStage{count: &count},
		&testCounterProcedureStage{count: &count, err: expectedErr},
		&testCounterProcedureStage{count: &count},
	}

	err := p.Execute()

	if !errors.Is(err, expectedErr) {
		t.Errorf("expected <<%+v>>, got %+v", expectedErr, err)
	}
	if count != 2 {
		t.Errorf("epected <<2>> error, got %#v", count)
	}
}

func TestLaunchCVDProcedureBuilder(t *testing.T) {
	abURL := "http://ab.test"
	cvdBinAB := AndroidBuild{ID: "1", Target: "xyzzy"}
	paths := IMPaths{
		CVDBin:           "/bin/cvd",
		ArtifactsRootDir: "/artifacts",
		HomesRootDir:     "/homes",
	}
	req := apiv1.CreateCVDRequest{
		BuildInfo: &apiv1.BuildInfo{BuildID: "256", Target: "waldo"},
	}

	t.Run("download cvd stage", func(t *testing.T) {
		builder := NewLaunchCVDProcedureBuilder(abURL, cvdBinAB, paths)
		p := builder.Build(req)

		s := p[0].(*StageDownloadCVD)

		if s.CVDBin != paths.CVDBin {
			t.Errorf("expected <<%q>>, got %q", paths.CVDBin, s.CVDBin)
		}
		if s.Build != cvdBinAB {
			t.Errorf("expected <<%+v>>, got %+v", cvdBinAB, s.Build)
		}
		if s.Downloader == nil {
			t.Error("expected not nil")
		}
	})

	t.Run("start cvd server stage", func(t *testing.T) {
		builder := NewLaunchCVDProcedureBuilder(abURL, cvdBinAB, paths)
		p := builder.Build(req)

		s := p[1].(*StageStartCVDServer)

		if s.CVDBin != paths.CVDBin {
			t.Errorf("expected <<%q>>, got %q", paths.CVDBin, s.CVDBin)
		}
	})

	t.Run("create artifacts root directory stage", func(t *testing.T) {
		builder := NewLaunchCVDProcedureBuilder(abURL, cvdBinAB, paths)
		p := builder.Build(req)

		s := p[2].(*StageCreateDirIfNotExist)

		if s.Dir != paths.ArtifactsRootDir {
			t.Errorf("expected <<%q>>, got %q", paths.ArtifactsRootDir, s.Dir)
		}
	})

	t.Run("create cvd artifacts directory stage", func(t *testing.T) {
		builder := NewLaunchCVDProcedureBuilder(abURL, cvdBinAB, paths)
		p := builder.Build(req)

		s := p[3].(*StageCreateDirIfNotExist)

		expected := "/artifacts/256_waldo"
		if s.Dir != expected {
			t.Errorf("expected <<%q>>, got %q", expected, s.Dir)
		}
	})

	t.Run("fetch cvd artifacts stage", func(t *testing.T) {
		builder := NewLaunchCVDProcedureBuilder(abURL, cvdBinAB, paths)
		p := builder.Build(req)

		s := p[4].(*StageFetchCVD)

		if s.CVDBin != paths.CVDBin {
			t.Errorf("expected <<%q>>, got %q", paths.CVDBin, s.CVDBin)
		}
		if s.BuildInfo != *req.BuildInfo {
			t.Errorf("expected <<%+v>>, got %+v", *req.BuildInfo, s.BuildInfo)
		}
		expectedOutDir := "/artifacts/256_waldo"
		if s.OutDir != expectedOutDir {
			t.Errorf("expected <<%q>>, got %q", expectedOutDir, s.OutDir)
		}
	})

	t.Run("fetch cvd artifacts stages with same build info", func(t *testing.T) {
		builder := NewLaunchCVDProcedureBuilder(abURL, cvdBinAB, paths)
		pFirst := builder.Build(req)
		pSecond := builder.Build(req)

		first := pFirst[4].(*StageFetchCVD)
		second := pSecond[4].(*StageFetchCVD)

		if first != second {
			t.Errorf("expected <<%+v>>, got %+v", first, second)
		}
	})

	t.Run("create homes root directory stage", func(t *testing.T) {
		builder := NewLaunchCVDProcedureBuilder(abURL, cvdBinAB, paths)
		p := builder.Build(req)

		s := p[5].(*StageCreateDirIfNotExist)

		if s.Dir != paths.HomesRootDir {
			t.Errorf("expected <<%q>>, got %q", paths.HomesRootDir, s.Dir)
		}
	})

	t.Run("create cvd home directory stage", func(t *testing.T) {
		builder := NewLaunchCVDProcedureBuilder(abURL, cvdBinAB, paths)
		p := builder.Build(req)

		s := p[6].(*StageCreateDirIfNotExist)

		expected := "/homes/cvd-1"
		if s.Dir != expected {
			t.Errorf("expected <<%q>>, got %q", expected, s.Dir)
		}
	})

	t.Run("create cvd home directory multiple times", func(t *testing.T) {
		builder := NewLaunchCVDProcedureBuilder(abURL, cvdBinAB, paths)
		for i := 0; i < 10; i++ {
			p := builder.Build(req)

			s := p[6].(*StageCreateDirIfNotExist)

			expected := fmt.Sprintf("/homes/cvd-%d", i+1)
			if s.Dir != expected {
				t.Errorf("expected <<%q>>, got %q", expected, s.Dir)
			}
		}
	})

	t.Run("launch cvd stage", func(t *testing.T) {
		builder := NewLaunchCVDProcedureBuilder(abURL, cvdBinAB, paths)
		for i := 0; i < 10; i++ {
			p := builder.Build(req)

			s := p[7].(*StageLaunchCVD)

			if s.CVDBin != paths.CVDBin {
				t.Errorf("expected <<%q>>, got %+q", paths.CVDBin, s.CVDBin)
			}
			var expectedN uint32 = uint32(i + 1)
			if s.InstanceNumber != expectedN {
				t.Errorf("expected <<%d>>, got %d", expectedN, s.InstanceNumber)
			}
			expectedArtDir := "/artifacts/256_waldo"
			if s.ArtifactsDir != expectedArtDir {
				t.Errorf("expected <<%q>>, got %+q", expectedArtDir, s.ArtifactsDir)
			}
			expectedHomeDir := fmt.Sprintf("/homes/cvd-%d", expectedN)
			if s.HomeDir != expectedHomeDir {
				t.Errorf("expected <<%q>>, got %+q", expectedHomeDir, s.HomeDir)
			}
		}
	})
}

func TestStageDownloadCVDDownloadFails(t *testing.T) {
	cvdBin := os.TempDir() + "/cvd"
	expectedErr := errors.New("error")
	s := StageDownloadCVD{
		CVDBin:     cvdBin,
		Build:      AndroidBuild{ID: "1", Target: "xyzzy"},
		Downloader: NewCVDDownloader(&AlwaysFailsArtifactDownloader{err: expectedErr}),
	}

	err := s.Run()

	if !errors.Is(err, expectedErr) {
		t.Errorf("expected <<%+v>>, got %+v", expectedErr, err)
	}
}

func TestStageDownloadCVD(t *testing.T) {
	dir := tempDir(t)
	defer removeDir(t, dir)
	cvdBin := dir + "/cvd"
	cvdBinContent := "foo"
	ad := &FakeArtifactDownloader{
		t:       t,
		content: cvdBinContent,
	}
	s := StageDownloadCVD{
		CVDBin:     cvdBin,
		Build:      AndroidBuild{ID: "1", Target: "xyzzy"},
		Downloader: NewCVDDownloader(ad),
	}

	err := s.Run()

	if err != nil {
		t.Errorf("expected <<nil>>, got %+v", err)
	}
	content, _ := ioutil.ReadFile(cvdBin)
	actual := string(content)
	if actual != cvdBinContent {
		t.Errorf("expected <<%q>>, got %q", cvdBinContent, actual)
	}
}

func TestStageStartCVDServerSucceeds(t *testing.T) {
	execContext := func(name string, args ...string) *exec.Cmd {
		return createFakeCmd(TestFakeCVDMain, name, args, t)
	}
	s := &StageStartCVDServer{
		ExecContext: execContext,
		CVDBin:      "/bin/foo",
	}

	err := s.Run()

	if err != nil {
		t.Errorf("expected <<nil>>, got %+v", err)
	}
}

// NOTE: This test is not a regular unit tests. It simulates a fake `cvd` alone execution.
// It validates the environment variables and arguments `cvd` should be called with.
func TestFakeCVDMain(t *testing.T) {
	// Early exist if called as a regular unit test function.
	if len(os.Args) < 3 || os.Args[2] != executedAsFakeMainArg {
		return
	}
	val, ok := os.LookupEnv(envVarAndroidHostOut)
	if !ok || val != "" {
		panic("invalid env var: " + envVarAndroidHostOut)
	}
	if os.Args[3] != "/bin/foo" {
		panic("invalid binary path")
	}
}

func TestStageFetchCVDSucceeds(t *testing.T) {
	execContext := func(name string, args ...string) *exec.Cmd {
		return createFakeCmd(TestFakeCVDFetchMain, name, args, t)
	}
	s := &StageFetchCVD{
		ExecContext: execContext,
		CVDBin:      "/bin/foo",
		BuildInfo:   apiv1.BuildInfo{BuildID: "256", Target: "bar"},
		OutDir:      "/tmp/baz",
	}

	err := s.Run()

	if err != nil {
		t.Errorf("expected <<nil>>, got %+v", err)
	}
}

// NOTE: This test is not a regular unit tests. It simulates a fake `cvd fetch` execution.
// It validates the environment variables and arguments `cvd fetch` should be called with.
func TestFakeCVDFetchMain(t *testing.T) {
	// Early exist if called as a regular unit test function.
	if len(os.Args) < 3 || os.Args[2] != executedAsFakeMainArg {
		return
	}
	val, ok := os.LookupEnv(envVarAndroidHostOut)
	if !ok || val != "" {
		panic("invalid env var: " + envVarAndroidHostOut)
	}
	if os.Args[3] != "/bin/foo" {
		panic("invalid binary path")
	}
	expectedArgs := []string{"fetch", "--default_build=256/bar", "--directory=/tmp/baz"}
	if !reflect.DeepEqual(os.Args[4:], expectedArgs) {
		panic("invalid arguments")
	}
}

const envVarCuttlefishTestEnvVar = "CUTTLEFISH_TEST_ENV_VAR"

func TestStageLaunchCVDSucceeds(t *testing.T) {
	execContext := func(name string, args ...string) *exec.Cmd {
		return createFakeCmd(TestFakeCVDStartMain, name, args, t)
	}
	s := &StageLaunchCVD{
		ExecContext:    execContext,
		CVDBin:         "/bin/foo",
		InstanceNumber: 1,
		ArtifactsDir:   "/tmp/bar",
		HomeDir:        "/tmp/baz",
	}
	// Tests that the current environment gets inherited.
	if err := os.Setenv(envVarCuttlefishTestEnvVar, ""); err != nil {
		t.Fatal(err)
	}
	// Test that the relevant environment variables values are overwritten.
	if err := os.Setenv(envVarAndroidHostOut, "FOO"); err != nil {
		t.Fatal(err)
	}

	err := s.Run()

	if err != nil {
		t.Errorf("expected <<nil>>, got %+v", err)
	}

	// Cleanup environment
	if err := os.Unsetenv(envVarCuttlefishTestEnvVar); err != nil {
		t.Fatal(err)
	}
	if err := os.Unsetenv(envVarAndroidHostOut); err != nil {
		t.Fatal(err)
	}
}

// NOTE: This test is not a regular unit tests. It simulates a fake `cvd start` execution.
// It validates the environment variables and arguments `cvd start` should be called with.
func TestFakeCVDStartMain(t *testing.T) {
	// Early exist if called as a regular unit test function.
	if len(os.Args) < 3 || os.Args[2] != executedAsFakeMainArg {
		return
	}
	if _, ok := os.LookupEnv(envVarCuttlefishTestEnvVar); !ok {
		panic("invalid env var: " + envVarCuttlefishTestEnvVar)
	}
	if os.Args[3] != "/bin/foo" {
		panic("invalid binary path")
	}
	if os.Getenv(envVarAndroidHostOut) != "/tmp/bar" {
		panic("invalid env var: " + envVarAndroidHostOut)
	}
	if os.Getenv(envVarHome) != "/tmp/baz" {
		panic("invalid env var: " + envVarAndroidHostOut)
	}
	expectedArgs := []string{
		"start",
		daemonArg, reportAnonymousUsageStatsArg,
		"--base_instance_num=1", "--system_image_dir=/tmp/bar",
	}
	if !reflect.DeepEqual(os.Args[4:], expectedArgs) {
		panic("invalid arguments")
	}
}

func TestStageCreateDirIfNotExist(t *testing.T) {
	tmpDir := tempDir(t)
	defer removeDir(t, tmpDir)
	dir := tmpDir + "/foo"
	s := StageCreateDirIfNotExist{Dir: dir}

	err := s.Run()

	if err != nil {
		t.Errorf("expected nil error, got %+v", err)
	}
	stats, _ := os.Stat(dir)
	expected := "drwxr-xr-x"
	got := stats.Mode().String()
	if got != expected {
		t.Errorf("expected <<%q>, got %q", expected, got)
	}
}

func TestStageCreateDirIfNotExistAndDirectoryExists(t *testing.T) {
	tmpDir := tempDir(t)
	defer removeDir(t, tmpDir)
	dir := tmpDir + "/foo"
	s := StageCreateDirIfNotExist{Dir: dir}

	err := s.Run()
	err = s.Run()

	if err != nil {
		t.Errorf("expected nil error, got %+v", err)
	}
	stats, _ := os.Stat(dir)
	expected := "drwxr-xr-x"
	got := stats.Mode().String()
	if got != expected {
		t.Errorf("expected <<%q>, got %q", expected, got)
	}
}

func TestStageCreateDirIfNotExistInvalidDir(t *testing.T) {
	s := StageCreateDirIfNotExist{Dir: ""}

	err := s.Run().(*os.PathError)

	if err.Op != "mkdir" {
		t.Errorf("expected <<%q>, got %q", "mkdir", err.Op)
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

func TestCVDDownloaderDownloadBinaryAlreadyExist(t *testing.T) {
	const fetchCVDContent = "bar"
	dir := tempDir(t)
	defer removeDir(t, dir)
	filename := dir + "/cvd"
	f, err := os.Create(filename)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	_, err = f.Write([]byte(fetchCVDContent))
	if err != nil {
		t.Fatal(err)
	}
	ad := &FakeArtifactDownloader{t, "foo"}
	cd := NewCVDDownloader(ad)

	err = cd.Download(filename, AndroidBuild{ID: "1", Target: "xyzzy"})

	if err != nil {
		t.Errorf("epected <<nil>> error, got %#v", err)
	}
	content, err := ioutil.ReadFile(filename)
	if err != nil {
		t.Fatal(err)
	}
	actual := string(content)
	if actual != fetchCVDContent {
		t.Errorf("expected <<%q>>, got %q", fetchCVDContent, actual)
	}
}

func TestCVDDownloaderDownload(t *testing.T) {
	dir := tempDir(t)
	defer removeDir(t, dir)
	filename := dir + "/cvd"
	ad := &FakeArtifactDownloader{t, "foo"}
	cd := NewCVDDownloader(ad)

	cd.Download(filename, AndroidBuild{ID: "1", Target: "xyzzy"})

	content, _ := ioutil.ReadFile(filename)
	actual := string(content)
	expected := "foo"
	if actual != expected {
		t.Errorf("expected <<%q>>, got %q", expected, actual)
	}
}

func TestCVDDownloaderDownload0750FileAccessIsSet(t *testing.T) {
	dir := tempDir(t)
	defer removeDir(t, dir)
	filename := dir + "/cvd"
	ad := &FakeArtifactDownloader{t, "foo"}
	cd := NewCVDDownloader(ad)

	cd.Download(filename, AndroidBuild{ID: "1", Target: "xyzzy"})

	stats, _ := os.Stat(filename)
	var expected os.FileMode = 0750
	if stats.Mode() != expected {
		t.Errorf("expected <<%+v>>, got %+v", expected, stats.Mode())
	}
}

func TestCVDDownloaderDownloadSettingFileAccessFails(t *testing.T) {
	dir := tempDir(t)
	defer removeDir(t, dir)
	filename := dir + "/cvd"
	ad := &FakeArtifactDownloader{t, "foo"}
	cd := NewCVDDownloader(ad)
	expectedErr := errors.New("error")
	cd.osChmod = func(_ string, _ os.FileMode) error {
		return expectedErr
	}

	err := cd.Download(filename, AndroidBuild{ID: "1", Target: "xyzzy"})

	if !errors.Is(err, expectedErr) {
		t.Errorf("expected <<%+v>>, got %+v", expectedErr, err)
	}
}

type AlwaysFailsArtifactDownloader struct {
	err error
}

func (d *AlwaysFailsArtifactDownloader) Download(_ io.Writer, _ AndroidBuild, _ string) error {
	return d.err
}

func TestCVDDownloaderDownloadingFails(t *testing.T) {
	dir := tempDir(t)
	defer removeDir(t, dir)
	filename := dir + "/cvd"
	expectedErr := errors.New("error")
	cd := NewCVDDownloader(&AlwaysFailsArtifactDownloader{err: expectedErr})

	err := cd.Download(filename, AndroidBuild{ID: "1", Target: "xyzzy"})

	if !errors.Is(err, expectedErr) {
		t.Errorf("expected <<%+v>>, got %+v", expectedErr, err)
	}
	if _, err := os.Stat(filename); err == nil {
		t.Errorf("file must not have been created")
	}
}

type roundTripFunc func(r *http.Request) (*http.Response, error)

func (s roundTripFunc) RoundTrip(r *http.Request) (*http.Response, error) {
	return s(r)
}

func newMockClient(rt roundTripFunc) *http.Client {
	return &http.Client{Transport: rt}
}

func newResponseBody(content string) io.ReadCloser {
	return ioutil.NopCloser(strings.NewReader(content))
}

func TestSignedURLArtifactDownloaderDownload(t *testing.T) {
	fetchCVDBinContent := "001100"
	getSignedURLRequestURI := "/android/internal/build/v3/builds/1/xyzzy/attempts/latest/artifacts/foo/url?redirect=false"
	downloadRequestURI := "/android-build/builds/X/Y/Z"
	url := "https://someurl.fake"
	mockClient := newMockClient(func(r *http.Request) (*http.Response, error) {
		res := &http.Response{
			StatusCode: http.StatusOK,
		}
		reqURI := r.URL.RequestURI()
		if reqURI == getSignedURLRequestURI {
			resURL := url + downloadRequestURI
			res.Body = newResponseBody(`{"signedUrl": "` + resURL + `"}`)
		} else if reqURI == downloadRequestURI {
			res.Body = newResponseBody(fetchCVDBinContent)
		} else {
			t.Fatalf("invalide request URI: %q\n", reqURI)
		}
		return res, nil
	})
	d := NewSignedURLArtifactDownloader(mockClient, url)

	var b bytes.Buffer
	d.Download(io.Writer(&b), AndroidBuild{ID: "1", Target: "xyzzy"}, "foo")

	actual := b.String()
	if actual != fetchCVDBinContent {
		t.Errorf("expected <<%q>>, got %q", fetchCVDBinContent, actual)
	}
}

func TestSignedURLArtifactDownloaderDownloadWithError(t *testing.T) {
	errorMessage := "No latest build attempt for build 1"
	url := "https://something.fake"
	mockClient := newMockClient(func(r *http.Request) (*http.Response, error) {
		errJSON := `{
			"error": {
				"code": 401,
				"message": "` + errorMessage + `"
			}
		}`
		return &http.Response{
			StatusCode: http.StatusNotFound,
			Body:       newResponseBody(errJSON),
		}, nil
	})
	d := NewSignedURLArtifactDownloader(mockClient, url)

	var b bytes.Buffer
	err := d.Download(io.Writer(&b), AndroidBuild{ID: "1", Target: "xyzzy"}, "foo")

	if !strings.Contains(err.Error(), errorMessage) {
		t.Errorf("expected to contain <<%q>> in error: %#v", errorMessage, err)
	}
}

func TestBuildGetSignedURL(t *testing.T) {
	baseURL := "http://localhost:1080"

	t.Run("regular build id", func(t *testing.T) {
		expected := "http://localhost:1080/android/internal/build/v3/builds/1/xyzzy/attempts/latest/artifacts/foo/url?redirect=false"

		actual := BuildGetSignedURL(baseURL, AndroidBuild{ID: "1", Target: "xyzzy"}, "foo")

		if actual != expected {
			t.Errorf("expected <<%q>>, got %q", expected, actual)
		}
	})

	t.Run("url-escaped android build params", func(t *testing.T) {
		expected := "http://localhost:1080/android/internal/build/v3/builds/1%3F/xyzzy%3F/attempts/latest/artifacts/foo/url?redirect=false"

		actual := BuildGetSignedURL(baseURL, AndroidBuild{ID: "1?", Target: "xyzzy?"}, "foo")

		if actual != expected {
			t.Errorf("expected <<%q>>, got %q", expected, actual)
		}
	})
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

type fakeMainFunc func(*testing.T)

const executedAsFakeMainArg = "executed_as_fake_main"

// Creates a new exec.Cmd, which will call the `TestMockGoTestCmdHelperFunction`
// function through the execution of the `go test` binary using the parameter `--test.run`.
func createFakeCmd(fn fakeMainFunc, name string, args []string, t *testing.T) *exec.Cmd {
	cs := []string{"--test.run=" + funcName(fn), executedAsFakeMainArg}
	verifyCmd := exec.Command(os.Args[0], cs...)
	err := verifyCmd.Run()
	if err == nil {
		// Makes sure the test function used as `fakeMainFunc` is picked by `go test`, otherwise the
		// execution will always succeed as no test was actually ran.
		t.Fatalf("execution of %s with no arguments or env variables set must fail", funcName(fn))
	}
	cs = append(cs, name)
	cs = append(cs, args...)
	cmd := exec.Command(os.Args[0], cs...)
	return cmd
}

func funcName(fn fakeMainFunc) string {
	name := runtime.FuncForPC(reflect.ValueOf(fn).Pointer()).Name()
	return name[strings.LastIndex(name, ".")+1:]
}
