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

package main

type InstanceManager struct{}

func (m *InstanceManager) CreateCVD(req *CreateCVDRequest) (*Operation, error) {
	if err := validateRequest(req); err != nil {
		return nil, err
	}
	return &Operation{}, nil
}

// TODO(b/226935747) Have more thorough validation error in Instance Manager.
var ErrBadCreateCVDRequest = NewBadRequestError("invalid CreateCVDRequest", nil)

func validateRequest(r *CreateCVDRequest) error {
	if r.BuildInfo == nil {
		return ErrBadCreateCVDRequest
	}
	if r.BuildInfo.BuildID == "" {
		return ErrBadCreateCVDRequest
	}
	if r.BuildInfo.Target == "" {
		return ErrBadCreateCVDRequest
	}
	if r.FetchCVDBuildID == "" {
		return ErrBadCreateCVDRequest
	}
	if r.BuildAPIAccessToken == "" {
		return ErrBadCreateCVDRequest
	}
	return nil
}
