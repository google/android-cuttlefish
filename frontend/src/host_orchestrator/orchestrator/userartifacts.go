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
	apiv1 "github.com/google/android-cuttlefish/frontend/src/liboperator/api/v1"
)

// Abstraction for managing user artifacts for launching CVDs.
type UserArtifactsManager interface {
	// Creates a new directory for uploading user artifacts in the future.
	NewDir() (*apiv1.UploadDirectory, error)
}

// Options for creating instances of UserArtifactsManager implementations.
type UserArtifactsManagerOpts struct {
	// The root directory where to store the artifacts.
	RootDir string
	// Factory of name values
	NamesFactory func() string
}

// An implementation of the UserArtifactsManager interface.
type UserArtifactsManagerImpl struct {
	UserArtifactsManagerOpts
}

// Creates a new instance of UserArtifactsManagerImpl.
func NewUserArtifactsManagerImpl(opts UserArtifactsManagerOpts) UserArtifactsManager {
	return &UserArtifactsManagerImpl{
		UserArtifactsManagerOpts: opts,
	}
}

func (m *UserArtifactsManagerImpl) NewDir() (*apiv1.UploadDirectory, error) {
	if err := createDir(m.RootDir, false); err != nil {
		return nil, err
	}
	name := m.NamesFactory()
	if err := createDir(m.RootDir+"/"+name, true); err != nil {
		return nil, err
	}
	return &apiv1.UploadDirectory{Name: name}, nil
}
