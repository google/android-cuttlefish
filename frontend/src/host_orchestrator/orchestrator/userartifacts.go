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
	"archive/zip"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"strings"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	hoexec "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/exec"
	"github.com/google/android-cuttlefish/frontend/src/liboperator/operator"
)

// Resolves the user artifacts full directory.
type UserArtifactsDirResolver interface {
	// Given a directory name returns its full path.
	GetDirPath(name string) string
}

type UserArtifactChunk struct {
	Name             string
	File             io.Reader
	UseChunkOffset   bool
	ChunkOffsetBytes int64

	ChunkNumber    int   // Deprecated: use `ChunkOffsetBytes`
	ChunkTotal     int   // Deprecated: use `OffsetBytes`
	ChunkSizeBytes int64 // Deprecated: use `OffsetBytes`
}

// Abstraction for managing user artifacts for launching CVDs.
type UserArtifactsManager interface {
	UserArtifactsDirResolver
	// Creates a new directory for uploading user artifacts in the future.
	NewDir() (*apiv1.UploadDirectory, error)
	// List existing directories
	ListDirs() (*apiv1.ListUploadDirectoriesResponse, error)
	// Update artifact with the passed chunk.
	UpdateArtifactWithDir(dir string, chunk UserArtifactChunk) error
	// Extract artifact
	ExtractArtifactWithDir(dir, name string) error
}

// Options for creating instances of UserArtifactsManager implementations.
type UserArtifactsManagerOpts struct {
	// The root directory where to store the artifacts.
	RootDir string
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
	dir, err := createNewUADir(m.RootDir)
	if err != nil {
		return nil, err
	}
	log.Println("created new user artifact directory", dir)
	return &apiv1.UploadDirectory{Name: filepath.Base(dir)}, nil
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

func (m *UserArtifactsManagerImpl) UpdateArtifactWithDir(dir string, chunk UserArtifactChunk) error {
	dir = m.RootDir + "/" + dir
	if ok, err := fileExist(dir); err != nil {
		return err
	} else if !ok {
		return operator.NewBadRequestError("upload directory %q does not exist", err)
	}
	filename := filepath.Join(dir, chunk.Name)
	if err := createUAFile(filename); err != nil {
		return err
	}
	if err := writeChunk(filename, chunk); err != nil {
		return err
	}
	return nil
}

func writeChunk(filename string, chunk UserArtifactChunk) error {
	f, err := os.OpenFile(filename, os.O_WRONLY, 0664)
	if err != nil {
		return err
	}
	defer f.Close()
	offset := chunk.ChunkOffsetBytes
	if !chunk.UseChunkOffset {
		log.Println("deprecated: use `ChunkOffsetBytes`")
		offset = int64(chunk.ChunkNumber-1) * chunk.ChunkSizeBytes
	}
	if _, err := f.Seek(offset, 0); err != nil {
		return err
	}
	if _, err = io.Copy(f, chunk.File); err != nil {
		return err
	}
	return nil
}

func (m *UserArtifactsManagerImpl) ExtractArtifactWithDir(dir, name string) error {
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
	_, err := hoexec.Exec(exec.CommandContext, "tar", "-xf", src, "-C", dst)
	if err != nil {
		return err
	}
	return nil
}

func Unzip(dstDir string, src string) error {
	r, err := zip.OpenReader(src)
	if err != nil {
		return err
	}
	defer r.Close()
	extractTo := func(dst string, src *zip.File) error {
		rc, err := src.Open()
		if err != nil {
			return err
		}
		defer rc.Close()
		if err := createUAFile(dst); err != nil {
			return err
		}
		dstFile, err := os.OpenFile(dst, os.O_WRONLY, 0664)
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
		if err := extractTo(filepath.Join(dstDir, f.Name), f); err != nil {
			return err
		}
	}
	return nil
}

func createNewUADir(parent string) (string, error) {
	ctx := exec.CommandContext
	stdout, err := hoexec.Exec(ctx, "mktemp", "--directory", "-p", parent)
	if err != nil {
		return "", err
	}
	name := strings.TrimRight(stdout, "\n")
	// Sets permission regardless of umask.
	if _, err := hoexec.Exec(ctx, "chmod", "u=rwx,g=rwx,o=r", name); err != nil {
		return "", err
	}
	return name, nil
}

func createUAFile(filename string) error {
	ctx := exec.CommandContext
	_, err := hoexec.Exec(ctx, "touch", filename)
	if err != nil {
		return err
	}
	// Sets permission regardless of umask.
	if _, err := hoexec.Exec(ctx, "chmod", "u=rwx,g=rw,o=r", filename); err != nil {
		return err
	}
	return nil
}
