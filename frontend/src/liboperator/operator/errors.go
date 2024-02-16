// Copyright 2022 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package operator

import (
	"net/http"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/liboperator/api/v1"
)

type AppError struct {
	Msg        string
	StatusCode int
	Err        error
}

func (e *AppError) Error() string {
	if e.Err != nil {
		return e.Msg + ": " + e.Err.Error()
	}
	return e.Msg
}

func (e *AppError) Unwrap() error {
	return e.Err
}

func (e *AppError) JSONResponse() apiv1.ErrorMsg {
	return apiv1.ErrorMsg{Error: e.Msg, Details: e.Error()}
}

func NewBadRequestError(msg string, e error) error {
	return &AppError{Msg: msg, StatusCode: http.StatusBadRequest, Err: e}
}

func NewInternalError(msg string, e error) error {
	return &AppError{Msg: msg, StatusCode: http.StatusInternalServerError, Err: e}
}

func NewNotFoundError(msg string, e error) error {
	return &AppError{Msg: msg, StatusCode: http.StatusNotFound, Err: e}
}

func NewServiceUnavailableError(msg string, e error) error {
	return &AppError{Msg: msg, StatusCode: http.StatusServiceUnavailable, Err: e}
}
