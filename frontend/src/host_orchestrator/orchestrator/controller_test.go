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
	"encoding/json"
	"io"
	"mime/multipart"
	"net/http"
	"net/http/httptest"
	"os"
	"strings"
	"testing"
	"time"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/debug"
	orchtesting "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/testing"

	"github.com/gorilla/mux"
)

const pageNotFoundErrMsg = "404 page not found\n"

func TestCreateCVDIsHandled(t *testing.T) {
	rr := httptest.NewRecorder()
	req, err := http.NewRequest("POST", "/cvds", strings.NewReader("{}"))
	if err != nil {
		t.Fatal(err)
	}
	controller := Controller{}

	makeRequest(rr, req, &controller)

	if rr.Code == http.StatusNotFound && rr.Body.String() == pageNotFoundErrMsg {
		t.Errorf("request was not handled. This failure implies an API breaking change.")
	}
}

func TestGetCVDLogsIsHandled(t *testing.T) {
	dir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, dir)
	rr := httptest.NewRecorder()
	req, err := http.NewRequest("GET", "/cvds/cvd-1/logs", strings.NewReader("{}"))
	if err != nil {
		t.Fatal(err)
	}
	controller := Controller{}

	makeRequest(rr, req, &controller)

	if rr.Code == http.StatusNotFound && rr.Body.String() == pageNotFoundErrMsg {
		t.Errorf("request was not handled. This failure implies an API breaking change.")
	}
}

func TestGetCVDIsHandled(t *testing.T) {
	rr := httptest.NewRecorder()
	req, err := http.NewRequest("GET", "/cvds/foo/bar", nil)
	if err != nil {
		t.Fatal(err)
	}
	controller := Controller{}

	makeRequest(rr, req, &controller)

	if rr.Code == http.StatusNotFound && rr.Body.String() == pageNotFoundErrMsg {
		t.Errorf("request was not handled. This failure implies an API breaking change.")
	}
}

func TestListInputDevicesIsHandled(t *testing.T) {
	for _, path := range []string{"/cvds/foo/bar/event_devices", "/cvds/foo/bar/event_devices"} {
		rr := httptest.NewRecorder()
		req, err := http.NewRequest("GET", path, nil)
		if err != nil {
			t.Fatal(err)
		}
		controller := Controller{}

		makeRequest(rr, req, &controller)

		if rr.Code == http.StatusNotFound && rr.Body.String() == pageNotFoundErrMsg {
			t.Errorf("request for path %q was not handled. This failure implies an API breaking change.", path)
		}
	}
}

func createMultipartEventsRequest(t *testing.T, method, path string, fileFieldName, fileName string, content []byte) *http.Request {
	t.Helper()
	body := &bytes.Buffer{}
	writer := multipart.NewWriter(body)
	if fileFieldName != "" {
		fw, err := writer.CreateFormFile(fileFieldName, fileName)
		if err != nil {
			t.Fatal(err)
		}
		if _, err := io.Copy(fw, bytes.NewReader(content)); err != nil {
			t.Fatal(err)
		}
	}
	if err := writer.Close(); err != nil {
		t.Fatal(err)
	}
	req, err := http.NewRequest(method, path, body)
	if err != nil {
		t.Fatal(err)
	}
	req.Header.Set("Content-Type", writer.FormDataContentType())
	return req
}

func TestInjectInputDeviceEventsIsHandled(t *testing.T) {
	paths := []string{
		"/cvds/foo/bar/event_devices/mouse:inject",
		"/cvds/foo/bar/event_devices/mouse/:inject",
	}
	for _, path := range paths {
		rr := httptest.NewRecorder()
		req := createMultipartEventsRequest(t, "POST", path, "file", "events.bin", []byte("fake binary event data"))
		controller := Controller{OperationManager: NewMapOM()}

		makeRequest(rr, req, &controller)

		if rr.Code == http.StatusNotFound && rr.Body.String() == pageNotFoundErrMsg {
			t.Errorf("request for path %q was not handled. This failure implies an API breaking change.", path)
		}
		if rr.Code != http.StatusOK {
			t.Errorf("expected status 200 for path %q, got %d: %s", path, rr.Code, rr.Body.String())
		}
		var op apiv1.Operation
		if err := json.Unmarshal(rr.Body.Bytes(), &op); err != nil {
			t.Fatalf("failed to decode response as Operation: %v", err)
		}
		if op.Name == "" {
			t.Errorf("expected non-empty operation name in response")
		}
	}
}

func TestInjectInputDeviceEventsNonMultipartFails(t *testing.T) {
	rr := httptest.NewRecorder()
	req, err := http.NewRequest("POST", "/cvds/foo/bar/event_devices/mouse:inject", strings.NewReader("raw binary"))
	if err != nil {
		t.Fatal(err)
	}
	req.Header.Set("Content-Type", "application/octet-stream")
	controller := Controller{OperationManager: NewMapOM()}

	makeRequest(rr, req, &controller)

	if rr.Code != http.StatusBadRequest {
		t.Errorf("expected status 400 for non-multipart request, got %d: %s", rr.Code, rr.Body.String())
	}
}

func TestSaveTempEventsFileNoExecutionPermissions(t *testing.T) {
	req := createMultipartEventsRequest(t, "POST", "/dummy", "file", "events.bin", []byte("binary content"))
	filePath, err := saveTempEventsFile(req)
	if err != nil {
		t.Fatalf("saveTempEventsFile failed: %v", err)
	}
	defer os.Remove(filePath)

	fi, err := os.Stat(filePath)
	if err != nil {
		t.Fatalf("failed to stat temp file: %v", err)
	}
	if fi.Mode().Perm()&0111 != 0 {
		t.Errorf("file has execution permissions: %v", fi.Mode().Perm())
	}
}

func TestSaveTempEventsFileNonMultipartFails(t *testing.T) {
	req, err := http.NewRequest("POST", "/dummy", strings.NewReader("binary content"))
	if err != nil {
		t.Fatal(err)
	}
	req.Header.Set("Content-Type", "application/octet-stream")
	_, err = saveTempEventsFile(req)
	if err == nil {
		t.Fatal("expected error for non-multipart request, got nil")
	}
}

func TestSaveTempEventsFileEmptyFileFails(t *testing.T) {
	req := createMultipartEventsRequest(t, "POST", "/dummy", "file", "events.bin", []byte(""))
	_, err := saveTempEventsFile(req)
	if err == nil {
		t.Fatal("expected error for empty file in multipart request, got nil")
	}
}

func TestSaveTempEventsFileMissingFileFails(t *testing.T) {
	req := createMultipartEventsRequest(t, "POST", "/dummy", "", "", nil)
	_, err := saveTempEventsFile(req)
	if err == nil {
		t.Fatal("expected error for missing file in multipart request, got nil")
	}
}

func TestGetOperationIsHandled(t *testing.T) {
	rr := httptest.NewRecorder()
	req, err := http.NewRequest("GET", "/operations/foo", nil)
	if err != nil {
		t.Fatal(err)
	}
	controller := Controller{OperationManager: NewMapOM()}

	makeRequest(rr, req, &controller)

	if rr.Code == http.StatusNotFound && rr.Body.String() == pageNotFoundErrMsg {
		t.Errorf("request was not handled. This failure implies an API breaking change.")
	}
}

func TestGetOperationsIsHandled(t *testing.T) {
	rr := httptest.NewRecorder()
	req, err := http.NewRequest("GET", "/operations", nil)
	if err != nil {
		t.Fatal(err)
	}
	controller := Controller{OperationManager: NewMapOM()}

	makeRequest(rr, req, &controller)

	if rr.Code == http.StatusNotFound && rr.Body.String() == pageNotFoundErrMsg {
		t.Errorf("request was not handled. This failure implies an API breaking change.")
	}
}

func TestGetOperationResultIsHandled(t *testing.T) {
	om := NewMapOM()
	op := om.New()
	om.Complete(op.Name, &OperationResult{Value: "bar"})
	rr := httptest.NewRecorder()
	req, err := http.NewRequest("GET", "/operations/"+op.Name+"/result", nil)
	if err != nil {
		t.Fatal(err)
	}
	controller := Controller{OperationManager: NewMapOM()}

	makeRequest(rr, req, &controller)

	if rr.Code == http.StatusNotFound && rr.Body.String() == pageNotFoundErrMsg {
		t.Errorf("request was not handled. This failure implies an API breaking change.")
	}
}

func TestWaitOperationIsHandled(t *testing.T) {
	rr := httptest.NewRecorder()
	req, err := http.NewRequest("POST", "/operations/foo/:wait", strings.NewReader(""))
	if err != nil {
		t.Fatal(err)
	}
	controller := Controller{OperationManager: NewMapOM()}

	makeRequest(rr, req, &controller)

	if rr.Code == http.StatusNotFound && rr.Body.String() == pageNotFoundErrMsg {
		t.Errorf("request was not handled. This failure implies an API breaking change.")
	}
}

func TestWaitOperationNotFound(t *testing.T) {
	rr := httptest.NewRecorder()
	req, err := http.NewRequest("POST", "/operations/foo/:wait", strings.NewReader(""))
	if err != nil {
		t.Fatal(err)
	}
	controller := Controller{OperationManager: NewMapOM()}

	makeRequest(rr, req, &controller)

	expected := http.StatusNotFound
	if rr.Code != expected {
		t.Errorf("expected <<%d>>, got %d", expected, rr.Code)
	}
}

func TestWaitOperationTimeout(t *testing.T) {
	rr := httptest.NewRecorder()
	dt := 100 * time.Millisecond
	om := NewMapOM()
	op := om.New()
	req, err := http.NewRequest("POST", "/operations/"+op.Name+"/:wait", strings.NewReader(""))
	if err != nil {
		t.Fatal(err)
	}
	controller := Controller{OperationManager: om, WaitOperationDuration: dt}

	start := time.Now()
	makeRequest(rr, req, &controller)
	duration := time.Since(start)

	expected := http.StatusServiceUnavailable
	if rr.Code != expected {
		t.Errorf("expected <<%d>>, got %d", expected, rr.Code)
	}
	if duration < dt {
		t.Error("wait deadline was not reached")
	}
}

func TestWaitOperationOperationIsDone(t *testing.T) {
	rr := httptest.NewRecorder()
	om := NewMapOM()
	op := om.New()
	om.Complete(op.Name, &OperationResult{Value: "foo"})
	req, err := http.NewRequest("POST", "/operations/"+op.Name+"/:wait", strings.NewReader(""))
	if err != nil {
		t.Fatal(err)
	}
	controller := Controller{OperationManager: om}

	makeRequest(rr, req, &controller)

	expected := http.StatusOK
	if rr.Code != expected {
		t.Errorf("expected <<%d>>, got %d", expected, rr.Code)
	}
}

type testUAM struct{}

func (testUAM) NewDir() (*apiv1.UploadDirectory, error) {
	return &apiv1.UploadDirectory{}, nil
}

func (testUAM) ListDirs() (*apiv1.ListUploadDirectoriesResponse, error) {
	return &apiv1.ListUploadDirectoriesResponse{}, nil
}

func (testUAM) UpdateArtifactWithDir(dir string, chunk UserArtifactChunk) error {
	return nil
}

func (testUAM) UpdateArtifact(checksum string, chunk UserArtifactChunk) error {
	return nil
}

func (testUAM) StatArtifact(checksum string) (*apiv1.StatArtifactResponse, error) {
	return &apiv1.StatArtifactResponse{}, nil
}

func (testUAM) ExtractArtifact(checksum string) error {
	return nil
}

func (testUAM) UpdatedArtifactPath(checksum string) string {
	return ""
}

func (testUAM) ExtractedArtifactPath(checksum string) string {
	return ""
}

func (testUAM) ExtractArtifactWithDir(string, string) error {
	return nil
}

type testIDM struct{}

func (testIDM) CreateImageDirectory() (string, error) {
	return "", nil
}

func (testIDM) ListImageDirectories() ([]string, error) {
	return []string{}, nil
}

func (testIDM) UpdateImageDirectory(imageDirName, dir string) error {
	return nil
}

func (testIDM) DeleteImageDirectory(imageDirName string) error {
	return nil
}

func TestUploadUserArtifactIsHandled(t *testing.T) {
	body := &bytes.Buffer{}
	writer := multipart.NewWriter(body)
	fw, _ := writer.CreateFormField("chunk_offset_bytes")
	io.Copy(fw, strings.NewReader("20"))
	fw, _ = writer.CreateFormField("file_size_bytes")
	io.Copy(fw, strings.NewReader("100"))
	fw, _ = writer.CreateFormFile("file", "foo.txt")
	io.Copy(fw, bytes.NewReader([]byte("lorem")))
	writer.Close()

	req, _ := http.NewRequest("PUT", "/v1/userartifacts/foo", bytes.NewReader(body.Bytes()))
	req.Header.Set("Content-Type", writer.FormDataContentType())
	controller := Controller{UserArtifactsManager: &testUAM{}}
	rr := httptest.NewRecorder()

	makeRequest(rr, req, &controller)

	if rr.Code != http.StatusOK {
		t.Errorf("request was not handled. This failure implies an API breaking change.")
	}
}

func TestStatUserArtifactIsHandled(t *testing.T) {
	req, err := http.NewRequest("GET", "/v1/userartifacts/foo", strings.NewReader("{}"))
	if err != nil {
		t.Fatal(err)
	}
	controller := Controller{UserArtifactsManager: &testUAM{}}
	rr := httptest.NewRecorder()

	makeRequest(rr, req, &controller)

	if rr.Code != http.StatusOK {
		t.Errorf("request was not handled. This failure implies an API breaking change.")
	}
}

func TestExtractUserArtifactIsHandled(t *testing.T) {
	rr := httptest.NewRecorder()
	req, err := http.NewRequest("POST", "/v1/userartifacts/foo/:extract", strings.NewReader("{}"))
	if err != nil {
		t.Fatal(err)
	}
	controller := Controller{UserArtifactsManager: &testUAM{}, OperationManager: NewMapOM()}

	makeRequest(rr, req, &controller)

	if rr.Code != http.StatusOK {
		t.Errorf("request was not handled. This failure implies an API breaking change.")
	}
}

func TestCreateImageDirectoryIsHandled(t *testing.T) {
	rr := httptest.NewRecorder()
	req, err := http.NewRequest("POST", "/cvd_imgs_dirs", strings.NewReader("{}"))
	if err != nil {
		t.Fatal(err)
	}
	controller := Controller{ImageDirectoriesManager: &testIDM{}, OperationManager: NewMapOM()}

	makeRequest(rr, req, &controller)

	if rr.Code != http.StatusOK {
		t.Errorf("request was not handled. This failure implies an API breaking change.")
	}
}

func TestListImageDirectoriesIsHandled(t *testing.T) {
	rr := httptest.NewRecorder()
	req, err := http.NewRequest("GET", "/cvd_imgs_dirs", strings.NewReader("{}"))
	if err != nil {
		t.Fatal(err)
	}
	controller := Controller{ImageDirectoriesManager: &testIDM{}}

	makeRequest(rr, req, &controller)

	if rr.Code != http.StatusOK {
		t.Errorf("request was not handled. This failure implies an API breaking change.")
	}
}

func TestUpdateImageDirectoryIsHandled(t *testing.T) {
	rr := httptest.NewRecorder()
	body, err := json.Marshal(apiv1.UpdateImageDirectoryRequest{UserArtifactChecksum: "aaa"})
	if err != nil {
		t.Fatal(err)
	}
	req, err := http.NewRequest("PUT", "/cvd_imgs_dirs/foo", bytes.NewBuffer(body))
	if err != nil {
		t.Fatal(err)
	}
	req.Header.Set("Content-Type", "application/json")
	controller := Controller{ImageDirectoriesManager: &testIDM{}, OperationManager: NewMapOM(), UserArtifactsManager: &testUAM{}}

	makeRequest(rr, req, &controller)

	if rr.Code != http.StatusOK {
		t.Errorf("request was not handled. This failure implies an API breaking change.")
	}
}

func TestDeleteImageDirectoryIsHandled(t *testing.T) {
	rr := httptest.NewRecorder()
	req, err := http.NewRequest("DELETE", "/cvd_imgs_dirs/foo", strings.NewReader("{}"))
	if err != nil {
		t.Fatal(err)
	}
	controller := Controller{ImageDirectoriesManager: &testIDM{}, OperationManager: NewMapOM()}

	makeRequest(rr, req, &controller)

	if rr.Code != http.StatusOK {
		t.Errorf("request was not handled. This failure implies an API breaking change.")
	}
}

func TestGetDebugVarzIsHandled(t *testing.T) {
	rr := httptest.NewRecorder()
	req, err := http.NewRequest("GET", "/_debug/varz", strings.NewReader("{}"))
	if err != nil {
		t.Fatal(err)
	}
	controller := Controller{DebugVariablesManager: debug.NewVariablesManager(debug.StaticVariables{})}

	makeRequest(rr, req, &controller)

	if rr.Code != http.StatusOK {
		t.Errorf("request was not handled. This failure implies an API breaking change.")
	}
}

func TestGetStatuszIsHandled(t *testing.T) {
	rr := httptest.NewRecorder()
	req, err := http.NewRequest("GET", "/_debug/statusz", nil)
	if err != nil {
		t.Fatal(err)
	}
	controller := Controller{}

	makeRequest(rr, req, &controller)

	if rr.Code != http.StatusOK {
		t.Errorf("request was not handled. This failure implies an API breaking change.")
	}
}

func makeRequest(w http.ResponseWriter, r *http.Request, controller *Controller) {
	router := mux.NewRouter()
	controller.AddRoutes(router)
	router.ServeHTTP(w, r)
}
