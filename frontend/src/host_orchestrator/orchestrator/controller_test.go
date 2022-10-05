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

	apiv1 "github.com/google/android-cuttlefish/frontend/src/liboperator/api/v1"

	"github.com/gorilla/mux"
)

type testIM struct{}

func (m *testIM) CreateCVD(req apiv1.CreateCVDRequest) (Operation, error) {
	return Operation{}, nil
}

func TestCreateCVDSucceeds(t *testing.T) {
	rr := httptest.NewRecorder()
	router := mux.NewRouter()
	req, err := http.NewRequest("POST", "/cvds", strings.NewReader("{}"))
	if err != nil {
		t.Fatal(err)
	}
	controller := Controller{InstanceManager: &testIM{}}
	controller.AddRoutes(router)

	router.ServeHTTP(rr, req)

	expected := http.StatusOK
	if rr.Code != expected {
		t.Errorf("unexpected status code <<%d>>, want: %d", rr.Code, expected)
	}
}

func TestGetOperationSucceeds(t *testing.T) {
	om := NewMapOM()
	op := om.New()
	rr := httptest.NewRecorder()
	router := mux.NewRouter()
	req, err := http.NewRequest("GET", "/operations/"+op.Name, nil)
	if err != nil {
		t.Fatal(err)
	}
	controller := Controller{OperationManager: om}
	controller.AddRoutes(router)

	router.ServeHTTP(rr, req)

	expected := http.StatusOK
	if rr.Code != expected {
		t.Errorf("unexpected status code <<%d>>, want: %d", rr.Code, expected)
	}
}
