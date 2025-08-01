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
