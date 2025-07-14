// Copyright 2025 Google LLC
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
	"fmt"
	"path/filepath"

	"github.com/google/uuid"
)

type ImageDirectoriesManager interface {
	// Create an empty image directory.
	CreateImageDirectory() (string, error)
}

// Options for creating instances of ImageDirectoriesManager implementations.
type ImageDirectoriesManagerOpts struct {
	// Root directory for storing image directories.
	RootDir string
}

// An implementation of the ImageDirectoriesManager interface.
type ImageDirectoriesManagerImpl struct {
	ImageDirectoriesManagerOpts
}

func NewImageDirectoriesManagerImpl(opts ImageDirectoriesManagerOpts) *ImageDirectoriesManagerImpl {
	return &ImageDirectoriesManagerImpl{ImageDirectoriesManagerOpts: opts}
}

func (m *ImageDirectoriesManagerImpl) CreateImageDirectory() (string, error) {
	if err := createDir(m.RootDir); err != nil {
		return "", fmt.Errorf("failed to create root directory for storing image directories: %w", err)
	}
	dirname := uuid.New().String()
	if err := createDir(filepath.Join(m.RootDir, dirname)); err != nil {
		return "", fmt.Errorf("failed to create an image directory: %w", err)
	}
	return dirname, nil
}
