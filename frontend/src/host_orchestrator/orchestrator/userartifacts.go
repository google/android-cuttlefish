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
	"archive/zip"
	"compress/gzip"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"path/filepath"
	"strings"
	"syscall"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/liboperator/api/v1"
	"github.com/google/android-cuttlefish/frontend/src/liboperator/operator"
)

// Resolves the user artifacts full directory.
type UserArtifactsDirResolver interface {
	// Given a directory name returns its full path.
	GetDirPath(name string) string
}

type UserArtifactChunk struct {
	Name           string
	ChunkNumber    int
	ChunkTotal     int
	ChunkSizeBytes int64
	File           io.Reader
}

// Abstraction for managing user artifacts for launching CVDs.
type UserArtifactsManager interface {
	UserArtifactsDirResolver
	// Creates a new directory for uploading user artifacts in the future.
	NewDir() (*apiv1.UploadDirectory, error)
	// List existing directories
	ListDirs() (*apiv1.ListUploadDirectoriesResponse, error)
	// Update artifact with the passed chunk.
	UpdateArtifact(dir string, chunk UserArtifactChunk) error
	// Extract artifact
	ExtractArtifact(dir, name string) error
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
	if err := createDir(m.RootDir); err != nil {
		return nil, err
	}
	name := m.NameFactory()
	if err := createNewDir(m.RootDir + "/" + name); err != nil {
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

func (m *UserArtifactsManagerImpl) GetDirPath(name string) string {
	return m.RootDir + "/" + name
}

func (m *UserArtifactsManagerImpl) GetFilePath(dir, filename string) string {
	return m.RootDir + "/" + dir + "/" + filename
}

func (m *UserArtifactsManagerImpl) UpdateArtifact(dir string, chunk UserArtifactChunk) error {
	dir = m.RootDir + "/" + dir
	if ok, err := fileExist(dir); err != nil {
		return err
	} else if !ok {
		return operator.NewBadRequestError("upload directory %q does not exist", err)
	}
	if err := writeChunk(dir, chunk); err != nil {
		return err
	}
	return nil
}

func writeChunk(dir string, chunk UserArtifactChunk) error {
	filename := dir + "/" + chunk.Name
	f, err := os.OpenFile(filename, os.O_WRONLY|os.O_CREATE, 0664)
	if err != nil {
		return err
	}
	defer f.Close()
	if _, err := f.Seek(int64(chunk.ChunkNumber-1)*chunk.ChunkSizeBytes, 0); err != nil {
		return err
	}
	if _, err = io.Copy(f, chunk.File); err != nil {
		return err
	}
	// Sets permission regardless of umask.
	return os.Chmod(filename, 0664)
}

func (m *UserArtifactsManagerImpl) ExtractArtifact(dir, name string) error {
	dir = filepath.Join(m.RootDir, dir)
	if ok, err := fileExist(dir); err != nil {
		return err
	} else if !ok {
		return operator.NewBadRequestError(fmt.Sprintf("directory %q does not exist", dir), nil)
	}
	filename := filepath.Join(dir, name)
	if ok, err := fileExist(filename); err != nil {
		return err
	} else if !ok {
		return operator.NewBadRequestError(fmt.Sprintf("artifact %q does not exist", name), nil)
	}
	if strings.HasSuffix(filename, ".tar.gz") {
		if err := Untar(dir, filename); err != nil {
			return fmt.Errorf("failed extracting %q: %w", name, err)
		}
	} else if strings.HasSuffix(filename, ".zip") {
		if err := Unzip(dir, filename); err != nil {
			return fmt.Errorf("failed extracting %q: %w", name, err)
		}
	} else {
		return operator.NewBadRequestError(fmt.Sprintf("unsupported extension: %q", name), nil)
	}
	return nil
}

func Untar(dst string, src string) error {
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
				oldmask := syscall.Umask(0)
				err := os.MkdirAll(target, 0774)
				syscall.Umask(oldmask)
				if err != nil {
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
		case tar.TypeSymlink:
			if err := os.Symlink(header.Linkname, target); err != nil {
				return err
			}
		}
	}
}

func Unzip(dstDir string, src string) error {
	r, err := zip.OpenReader(src)
	if err != nil {
		return err
	}
	defer r.Close()
	extractAndCopy := func(dst string, src *zip.File) error {
		rc, err := src.Open()
		if err != nil {
			return err
		}
		defer rc.Close()
		dstFile, err := os.OpenFile(dst, os.O_CREATE|os.O_RDWR, os.FileMode(src.Mode()))
		if err != nil {
			return err
		}
		defer dstFile.Close()
		if _, err := io.Copy(dstFile, rc); err != nil {
			return err
		}
		return nil
	}
	for _, f := range r.File {
		// Do not extract nested dirs as ci.android.com img.zip artifact
		// does not contain nested dirs.
		if f.Mode().IsDir() {
			continue
		}
		if err := extractAndCopy(filepath.Join(dstDir, f.Name), f); err != nil {
			return err
		}
	}
	return nil
}
