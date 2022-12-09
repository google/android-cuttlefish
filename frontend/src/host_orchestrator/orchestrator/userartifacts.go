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
	"archive/tar"
	"compress/gzip"
	"io"
	"io/ioutil"
	"os"
	"path/filepath"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/liboperator/api/v1"
	"github.com/google/android-cuttlefish/frontend/src/liboperator/operator"
)

// Abstraction for managing user artifacts for launching CVDs.
type UserArtifactsManager interface {
	// Creates a new directory for uploading user artifacts in the future.
	NewDir() (*apiv1.UploadDirectory, error)
	// List existing directories
	ListDirs() (*apiv1.ListUploadDirectoriesResponse, error)
	// Creates or update (if exists) an artifact.
	CreateUpdateArtifact(dir, filename string, file io.Reader) error
}

// Options for creating instances of UserArtifactsManager implementations.
type UserArtifactsManagerOpts struct {
	// The root directory where to store the artifacts.
	RootDir string
	// Factory of name values
	NameFactory func() string
}

// An implementation of the UserArtifactsManager interface.
type UserArtifactsManagerImpl struct {
	UserArtifactsManagerOpts
}

// Creates a new instance of UserArtifactsManagerImpl.
func NewUserArtifactsManagerImpl(opts UserArtifactsManagerOpts) *UserArtifactsManagerImpl {
	return &UserArtifactsManagerImpl{
		UserArtifactsManagerOpts: opts,
	}
}

func (m *UserArtifactsManagerImpl) NewDir() (*apiv1.UploadDirectory, error) {
	if err := createDir(m.RootDir, false); err != nil {
		return nil, err
	}
	name := m.NameFactory()
	if err := createDir(m.RootDir+"/"+name, true); err != nil {
		return nil, err
	}
	return &apiv1.UploadDirectory{Name: name}, nil
}

func (m *UserArtifactsManagerImpl) ListDirs() (*apiv1.ListUploadDirectoriesResponse, error) {
	exist, err := fileExist(m.RootDir)
	if err != nil {
		return nil, err
	}
	if !exist {
		return &apiv1.ListUploadDirectoriesResponse{Items: make([]*apiv1.UploadDirectory, 0)}, nil
	}
	entries, err := ioutil.ReadDir(m.RootDir)
	if err != nil {
		return nil, err
	}
	dirs := make([]*apiv1.UploadDirectory, 0)
	for _, entry := range entries {
		if entry.IsDir() {
			dirs = append(dirs, &apiv1.UploadDirectory{Name: entry.Name()})
		}
	}
	return &apiv1.ListUploadDirectoriesResponse{Items: dirs}, nil
}

func (m *UserArtifactsManagerImpl) GetDirPath(dir string) string {
	return m.RootDir + "/" + dir
}

func (m *UserArtifactsManagerImpl) GetFilePath(dir, filename string) string {
	return m.RootDir + "/" + dir + "/" + filename
}

func (m *UserArtifactsManagerImpl) CreateUpdateArtifact(dir, name string, src io.Reader) error {
	dir = m.RootDir + "/" + dir
	if ok, err := fileExist(dir); err != nil {
		return err
	} else if !ok {
		return operator.NewBadRequestError("upload directory %q does not exist", err)
	}
	dst := dir + "/" + name
	if err := saveFile(dst, src); err != nil {
		return err
	}
	return processFile(dst)
}

func saveFile(dst string, src io.Reader) error {
	tmp, err := ioutil.TempFile(filepath.Dir(dst), filepath.Base(dst))
	if err != nil {
		return err
	}
	defer os.Remove(tmp.Name())
	if _, err = io.Copy(tmp, src); err != nil {
		return err
	}
	if err := tmp.Close(); err != nil {
		return err
	}
	return os.Rename(tmp.Name(), dst)
}

const cvdHostPackageName = "cvd-host_package.tar.gz"

func processFile(name string) error {
	if filepath.Base(name) == cvdHostPackageName {
		if err := untar(filepath.Dir(name), name); err != nil {
			return err
		}
		if err := os.Remove(name); err != nil {
			return err
		}
	}
	return nil
}

func untar(dst string, src string) error {
	r, err := os.Open(src)
	if err != nil {
		return err
	}
	defer r.Close()
	gzr, err := gzip.NewReader(r)
	if err != nil {
		return err
	}
	defer gzr.Close()
	tr := tar.NewReader(gzr)
	for {
		header, err := tr.Next()
		if err == io.EOF {
			return nil
		}
		if err != nil {
			return err
		}
		if header == nil {
			continue
		}
		target := filepath.Join(dst, header.Name)
		switch header.Typeflag {
		case tar.TypeDir:
			if _, err := os.Stat(target); err != nil {
				if err := os.MkdirAll(target, 0774); err != nil {
					return err
				}
			}
		case tar.TypeReg:
			f, err := os.OpenFile(target, os.O_CREATE|os.O_RDWR, os.FileMode(header.Mode))
			if err != nil {
				return err
			}
			if _, err := io.Copy(f, tr); err != nil {
				return err
			}
			f.Close()
		}
	}
}
