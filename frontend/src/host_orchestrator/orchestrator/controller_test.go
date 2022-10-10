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
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/liboperator/api/v1"

	"github.com/gorilla/mux"
)

const pageNotFoundErrMsg = "404 page not found\n"

type testIM struct{}

func (m *testIM) CreateCVD(req apiv1.CreateCVDRequest) (Operation, error) {
	return Operation{}, nil
}

func TestCreateCVDIsHandled(t *testing.T) {
	rr := httptest.NewRecorder()
	req, err := http.NewRequest("POST", "/cvds", strings.NewReader("{}"))
	if err != nil {
		t.Fatal(err)
	}
	controller := Controller{InstanceManager: &testIM{}}

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

func TestWaitOperationIsHandled(t *testing.T) {
	rr := httptest.NewRecorder()
	req, err := http.NewRequest("POST", "/operations/foo/wait", strings.NewReader(""))
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
	req, err := http.NewRequest("POST", "/operations/foo/wait", strings.NewReader(""))
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
	req, err := http.NewRequest("POST", "/operations/"+op.Name+"/wait", strings.NewReader(""))
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
	om.Complete(op.Name, OperationResult{Error: OperationResultError{"error"}})
	req, err := http.NewRequest("POST", "/operations/"+op.Name+"/wait", strings.NewReader(""))
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

func makeRequest(w http.ResponseWriter, r *http.Request, controller *Controller) {
	router := mux.NewRouter()
	controller.AddRoutes(router)
	router.ServeHTTP(w, r)
}
