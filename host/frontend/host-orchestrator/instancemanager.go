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

func validateRequest(r *CreateCVDRequest) error {
	if r.BuildInfo == nil ||
		r.BuildInfo.BuildID == "" ||
		r.BuildInfo.Target == "" ||
		r.FetchCVDBuildID == "" ||
		r.BuildAPIAccessToken == "" {
		return NewBadRequestError("invalid CreateCVDRequest", nil)
	}
	return nil
}
