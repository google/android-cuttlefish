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
	"io"
	"io/ioutil"
	"net/http"
	"os"
	"os/exec"
	"strings"
	"sync"
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
		FetchCVDBuildID: "9999",
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
		{func(r *apiv1.CreateCVDRequest) { r.FetchCVDBuildID = "" }},
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
		FetchCVDBuildID: "1",
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
		FetchCVDBuildID: "1",
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
	cvdBuildID := "1"
	paths := IMPaths{
		CVDBin:           "bin/cvd",
		ArtifactsRootDir: "ard",
		HomesRootDir:     "hrd",
	}
	cvdDownloader := NewCVDDownloader(&AlwaysFailsArtifactDownloader{err: errors.New("error")})
	startCVDServerCmd := &CVDSubcmdStartCVDServer{}
	builder := LaunchCVDProcedureBuilder{
		Paths:             paths,
		CVDDownloader:     cvdDownloader,
		StartCVDServerCmd: startCVDServerCmd,
	}

	t.Run("first stage is download cvd", func(t *testing.T) {
		p := builder.Build(apiv1.CreateCVDRequest{FetchCVDBuildID: cvdBuildID})

		_, ok := p[0].(*StageDownloadCVD)

		if !ok {
			t.Errorf("expected <<%T>>, got %T", &StageDownloadCVD{}, p[0])
		}
	})

	t.Run("download cvd stage", func(t *testing.T) {
		p := builder.Build(apiv1.CreateCVDRequest{FetchCVDBuildID: cvdBuildID})

		s := p[0].(*StageDownloadCVD)
		if s.CVDBin != paths.CVDBin {
			t.Errorf("expected <<%q>>, got %q", paths.CVDBin, s.CVDBin)
		}
		if s.BuildID != cvdBuildID {
			t.Errorf("expected <<%q>>, got %q", cvdBuildID, s.BuildID)
		}
		if s.Downloader != cvdDownloader {
			t.Errorf("expected <<%+v>>, got %+v", cvdDownloader, s.Downloader)
		}
		if s.Mutex == nil {
			t.Error("expected non nil")
		}
	})

	t.Run("download cvd stages have same mutex", func(t *testing.T) {
		p1 := builder.Build(apiv1.CreateCVDRequest{FetchCVDBuildID: cvdBuildID})
		p2 := builder.Build(apiv1.CreateCVDRequest{FetchCVDBuildID: cvdBuildID})
		first := p1[0].(*StageDownloadCVD)
		second := p2[0].(*StageDownloadCVD)

		if first == second {
			t.Error("expected different stages")
		}
		if first.Mutex != second.Mutex {
			t.Error("expected the same mutex")
		}
	})

	t.Run("second stage is start cvd server", func(t *testing.T) {
		p := builder.Build(apiv1.CreateCVDRequest{FetchCVDBuildID: cvdBuildID})

		_, ok := p[1].(*StageStartCVDServer)

		if !ok {
			t.Errorf("expected <<%T>>, got %T", &StageStartCVDServer{}, p[1])
		}
	})

	t.Run("start cvd server stage", func(t *testing.T) {
		p := builder.Build(apiv1.CreateCVDRequest{FetchCVDBuildID: cvdBuildID})

		s := p[1].(*StageStartCVDServer)
		if s.StartCVDServerCmd != startCVDServerCmd {
			t.Errorf("expected <<%q>>, got %q", startCVDServerCmd, s.StartCVDServerCmd)
		}
		if s.Mutex == nil {
			t.Error("expected non nil")
		}
		if s.Started == nil {
			t.Error("expected non nil")
		}
	})

	t.Run("start cvd server stages have same mutex", func(t *testing.T) {
		p1 := builder.Build(apiv1.CreateCVDRequest{FetchCVDBuildID: cvdBuildID})
		p2 := builder.Build(apiv1.CreateCVDRequest{FetchCVDBuildID: cvdBuildID})
		first := p1[1].(*StageStartCVDServer)
		second := p2[1].(*StageStartCVDServer)

		if first == second {
			t.Error("expected different stages")
		}
		if first.Mutex != second.Mutex {
			t.Error("expected the same mutex")
		}
	})

	t.Run("start cvd server stages have same started pointer", func(t *testing.T) {
		p1 := builder.Build(apiv1.CreateCVDRequest{FetchCVDBuildID: cvdBuildID})
		p2 := builder.Build(apiv1.CreateCVDRequest{FetchCVDBuildID: cvdBuildID})
		first := p1[1].(*StageStartCVDServer)
		second := p2[1].(*StageStartCVDServer)

		if first == second {
			t.Error("expected different stages")
		}
		if first.Started != second.Started {
			t.Error("expected the same started pointer")
		}
	})

	t.Run("download cvd and start cvd server stages have different mutexes", func(t *testing.T) {
		p1 := builder.Build(apiv1.CreateCVDRequest{FetchCVDBuildID: cvdBuildID})
		download := p1[0].(*StageDownloadCVD)
		startServer := p1[1].(*StageStartCVDServer)

		if download.Mutex == startServer.Mutex {
			t.Error("expected different mutexes")
		}
	})

	t.Run("create artifacts root directory stage", func(t *testing.T) {
		p := builder.Build(apiv1.CreateCVDRequest{FetchCVDBuildID: cvdBuildID})

		s, ok := p[2].(*StageCreateDirIfNotExist)

		if !ok {
			t.Errorf("expected <<%T>>, got %T", &StageCreateDirIfNotExist{}, p[2])
		}
		if s.Dir != paths.ArtifactsRootDir {
			t.Errorf("expected <<%q>>, got %q", paths.ArtifactsRootDir, s.Dir)
		}
	})

	t.Run("create homes root directory stage", func(t *testing.T) {
		p := builder.Build(apiv1.CreateCVDRequest{FetchCVDBuildID: cvdBuildID})

		s, ok := p[3].(*StageCreateDirIfNotExist)

		if !ok {
			t.Errorf("expected <<%T>>, got %T", &StageCreateDirIfNotExist{}, p[3])
		}
		if s.Dir != paths.HomesRootDir {
			t.Errorf("expected <<%q>>, got %q", paths.HomesRootDir, s.Dir)
		}
	})
}

func TestStageDownloadCVDDownloadFails(t *testing.T) {
	dir := t.TempDir()
	cvdBin := dir + "/cvd"
	expectedErr := errors.New("error")
	s := StageDownloadCVD{
		CVDBin:     cvdBin,
		BuildID:    "1",
		Downloader: NewCVDDownloader(&AlwaysFailsArtifactDownloader{err: expectedErr}),
		Mutex:      &sync.Mutex{},
	}

	err := s.Run()

	if !errors.Is(err, expectedErr) {
		t.Errorf("expected <<%+v>>, got %+v", expectedErr, err)
	}
}

func TestStageDownloadCVD(t *testing.T) {
	dir := t.TempDir()
	cvdBin := dir + "/cvd"
	cvdBinContent := "foo"
	ad := &FakeArtifactDownloader{
		t:       t,
		content: cvdBinContent,
	}
	s := StageDownloadCVD{
		CVDBin:     cvdBin,
		BuildID:    "1",
		Downloader: NewCVDDownloader(ad),
		Mutex:      &sync.Mutex{},
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

func TestStageCreateDirIfNotExist(t *testing.T) {
	dir := t.TempDir() + "/foo"
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
	dir := t.TempDir() + "/foo"
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

type FakeArtifactDownloader struct {
	t       *testing.T
	content string
}

func (d *FakeArtifactDownloader) Download(dst io.Writer, buildID, name string) error {
	r := strings.NewReader(d.content)
	if _, err := io.Copy(dst, r); err != nil {
		d.t.Fatal(err)
	}
	return nil
}

func TestCVDDownloaderDownloadBinaryAlreadyExist(t *testing.T) {
	const fetchCVDContent = "bar"
	dir := t.TempDir()
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

	err = cd.Download(filename, "1")

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
	dir := t.TempDir()
	filename := dir + "/cvd"
	ad := &FakeArtifactDownloader{t, "foo"}
	cd := NewCVDDownloader(ad)

	cd.Download(filename, "1")

	content, _ := ioutil.ReadFile(filename)
	actual := string(content)
	expected := "foo"
	if actual != expected {
		t.Errorf("expected <<%q>>, got %q", expected, actual)
	}
}

func TestCVDDownloaderDownload0750FileAccessIsSet(t *testing.T) {
	dir := t.TempDir()
	filename := dir + "/cvd"
	ad := &FakeArtifactDownloader{t, "foo"}
	cd := NewCVDDownloader(ad)

	cd.Download(filename, "1")

	stats, _ := os.Stat(filename)
	var expected os.FileMode = 0750
	if stats.Mode() != expected {
		t.Errorf("expected <<%+v>>, got %+v", expected, stats.Mode())
	}
}

func TestCVDDownloaderDownloadSettingFileAccessFails(t *testing.T) {
	dir := t.TempDir()
	filename := dir + "/cvd"
	ad := &FakeArtifactDownloader{t, "foo"}
	cd := NewCVDDownloader(ad)
	expectedErr := errors.New("error")
	cd.osChmod = func(_ string, _ os.FileMode) error {
		return expectedErr
	}

	err := cd.Download(filename, "1")

	if !errors.Is(err, expectedErr) {
		t.Errorf("expected <<%+v>>, got %+v", expectedErr, err)
	}
}

type AlwaysFailsArtifactDownloader struct {
	err error
}

func (d *AlwaysFailsArtifactDownloader) Download(dst io.Writer, buildID, name string) error {
	return d.err
}

func TestCVDDownloaderDownloadingFails(t *testing.T) {
	dir := t.TempDir()
	filename := dir + "/cvd"
	expectedErr := errors.New("error")
	cd := NewCVDDownloader(&AlwaysFailsArtifactDownloader{err: expectedErr})

	err := cd.Download(filename, "1")

	if !errors.Is(err, expectedErr) {
		t.Errorf("expected <<%+v>>, got %+v", expectedErr, err)
	}
	if _, err := os.Stat(filename); err == nil {
		t.Errorf("file must not have been created")
	}
}

func TestCVDSubcmdStartCVDServerVerifyArgs(t *testing.T) {
	var usedCommand string
	var usedArgs []string
	cvdBin := "cvd"
	execContext := func(command string, args ...string) *exec.Cmd {
		usedCommand = command
		usedArgs = args
		return createMockGoTestCmd()
	}
	cmd := CVDSubcmdStartCVDServer{CVDBin: cvdBin}

	cmd.Run(execContext)

	if usedCommand != cvdBin {
		t.Errorf("expected <<%q>>, got %q", cvdBin, usedCommand)
	}
	if len(usedArgs) > 0 {
		t.Errorf("expected empty args, got %v", usedArgs)
	}
}

func TestCVDSubcmdStartCVDServer(t *testing.T) {
	execContext := func(command string, args ...string) *exec.Cmd {
		return createMockGoTestCmd()
	}
	cmd := CVDSubcmdStartCVDServer{CVDBin: "cvd"}

	err := cmd.Run(execContext)

	if err != nil {
		t.Errorf("epected <<nil>> error, got %+v", err)
	}
}

func TestCVDSubcmdStartCVDServerExecutionFails(t *testing.T) {
	execContext := func(command string, args ...string) *exec.Cmd {
		return createFailingMockGoTestCmd()
	}
	cmd := CVDSubcmdStartCVDServer{CVDBin: "cvd"}

	err := cmd.Run(execContext)

	var execErr *exec.ExitError
	if !errors.As(err, &execErr) {
		t.Errorf("error type <<\"%T\">> not found in error chain", execErr)
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
	getSignedURLRequestURI := "/android/internal/build/v3/builds/1/aosp_cf_x86_64_phone-userdebug/attempts/latest/artifacts/foo/url?redirect=false"
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
	d.Download(io.Writer(&b), "1", "foo")

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
	err := d.Download(io.Writer(&b), "1", "foo")

	if !strings.Contains(err.Error(), errorMessage) {
		t.Errorf("expected to contain <<%q>> in error: %#v", errorMessage, err)
	}
}

func TestBuildGetSignedURL(t *testing.T) {
	baseURL := "http://localhost:1080"

	t.Run("regular build id", func(t *testing.T) {
		expected := "http://localhost:1080/android/internal/build/v3/builds/1/aosp_cf_x86_64_phone-userdebug/attempts/latest/artifacts/foo/url?redirect=false"

		actual := BuildGetSignedURL(baseURL, "1", "foo")

		if actual != expected {
			t.Errorf("expected <<%q>>, got %q", expected, actual)
		}
	})

	t.Run("url-escaped build id", func(t *testing.T) {
		expected := "http://localhost:1080/android/internal/build/v3/builds/latest%3F/aosp_cf_x86_64_phone-userdebug/attempts/latest/artifacts/foo/url?redirect=false"

		actual := BuildGetSignedURL(baseURL, "latest?", "foo")

		if actual != expected {
			t.Errorf("expected <<%q>>, got %q", expected, actual)
		}
	})
}

// Creates a new exec.Cmd, which will call the `TestMockGoTestCmdHelperFunction`
// function through the execution of the `go test` binary using the parameter `--test.run`.
func createMockGoTestCmd() *exec.Cmd {
	cs := []string{"--test.run=TestMockGoTestCmdHelperFunction"}
	cmd := exec.Command(os.Args[0], cs...)
	cmd.Env = []string{"EXECUTED_AS_MOCK_GO_TEST_CMD=1"}
	return cmd
}

// Creates a new exec.Cmd, which will call the `TestMockGoTestCmdHelperFunction`
// function through the execution of the `go test` binary using the parameter `--test.run`.
// The execution of this command will fail.
func createFailingMockGoTestCmd() *exec.Cmd {
	cs := []string{"--test.run=TestMockGoTestCmdHelperFunction"}
	cmd := exec.Command(os.Args[0], cs...)
	cmd.Env = []string{"EXECUTED_AS_MOCK_GO_TEST_CMD=1", "EXECUTION_FAILS=1"}
	return cmd
}

// NOTE: This is not a regular unit tests. This is a helper test meant to be called
// when executing the command returned in `createTestCmd`.
func TestMockGoTestCmdHelperFunction(t *testing.T) {
	// Early exist if called as a regular unit test function.
	if os.Getenv("EXECUTED_AS_MOCK_GO_TEST_CMD") != "1" {
		return
	}
	if os.Getenv("EXECUTION_FAILS") == "1" {
		panic("fails")
	}
}
