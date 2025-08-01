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
	"io/ioutil"
	"os"
	"path/filepath"

	"github.com/google/android-cuttlefish/frontend/src/liboperator/operator"
	"github.com/google/uuid"
)

type ImageDirectoriesManager interface {
	// Create an empty image directory.
	CreateImageDirectory() (string, error)
	// List all image directories.
	ListImageDirectories() ([]string, error)
	// Update image directory with creating or modifying symlinks of all files
	// under specified directory.
	UpdateImageDirectory(imageDirName, dir string) error
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

func (m *ImageDirectoriesManagerImpl) ListImageDirectories() ([]string, error) {
	imageDirs := []string{}
	if exists, err := dirExists(m.RootDir); err != nil {
		return nil, fmt.Errorf("failed to check existence of directory: %w", err)
	} else if !exists {
		return imageDirs, nil
	}
	entries, err := ioutil.ReadDir(m.RootDir)
	if err != nil {
		return nil, fmt.Errorf("failed to read directory: %w", err)
	}
	for _, entry := range entries {
		if !entry.IsDir() {
			return nil, fmt.Errorf("invalid image directory: %q", entry.Name())
		}
		imageDirs = append(imageDirs, entry.Name())
	}
	return imageDirs, nil
}

func (m *ImageDirectoriesManagerImpl) UpdateImageDirectory(imageDirName, dir string) error {
	if exists, err := dirExists(dir); err != nil {
		return fmt.Errorf("failed to check existence of directory: %w", err)
	} else if !exists {
		return operator.NewNotFoundError(fmt.Sprintf("directory(dir:%q) not found", dir), nil)
	}
	imageDir := filepath.Join(m.RootDir, imageDirName)
	if exists, err := dirExists(imageDir); err != nil {
		return fmt.Errorf("failed to check existence of directory: %w", err)
	} else if !exists {
		return operator.NewNotFoundError(fmt.Sprintf("image directory(dir:%q) not found", imageDirName), nil)
	}
	entries, err := ioutil.ReadDir(dir)
	if err != nil {
		return fmt.Errorf("failed to read directory: %w", err)
	}
	for _, entry := range entries {
		symlink := filepath.Join(imageDir, entry.Name())
		if exists, err := fileExist(symlink); err != nil {
			return fmt.Errorf("failed to check existence of file: %w", err)
		} else if exists {
			if err := os.Remove(symlink); err != nil {
				return fmt.Errorf("failed to remove previous symlink: %w", err)
			}
		}
		if err := os.Symlink(filepath.Join(dir, entry.Name()), symlink); err != nil {
			return fmt.Errorf("failed to create symlink: %w", err)
		}
	}
	return nil
}
