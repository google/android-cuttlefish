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
	"context"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"os"
	"os/exec"
	"os/user"
	"path/filepath"
	"strings"
	"time"

	"github.com/gofrs/flock"
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
	NewDir(dir string) (*apiv1.UploadDirectory, error)
	// Acquire lock on the directory for preventing races.
	LockFile(dir, name string) (*apiv1.LockFileResponse, error)
	// Release lock on the directory locked for preventing races.
	UnlockFile(dir, name string) (*apiv1.LockFileResponse, error)
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
	// Artifact owner. If nil, the owner will be the process's owner.
	Owner *user.User
}

// An implementation of the UserArtifactsManager interface.
type UserArtifactsManagerImpl struct {
	UserArtifactsManagerOpts

	locks map[string]*flock.Flock
}

// Creates a new instance of UserArtifactsManagerImpl.
func NewUserArtifactsManagerImpl(opts UserArtifactsManagerOpts) *UserArtifactsManagerImpl {
	return &UserArtifactsManagerImpl{
		UserArtifactsManagerOpts: opts,
		locks: make(map[string]*flock.Flock),
	}
}

const (
	lockDir = ".lock"
)

func (m *UserArtifactsManagerImpl) NewDir(dir string) (*apiv1.UploadDirectory, error) {
	if dir == lockDir {
		return nil, fmt.Errorf("invalid dir name %q", dir)
	}
	if err := createDir(m.RootDir); err != nil {
		return nil, err
	}
	if err := createDir(filepath.Join(m.RootDir, lockDir)); err != nil {
		return nil, err
	}
	dir, err := createNewUADir(m.RootDir, dir, m.Owner)
	if err != nil {
		return nil, err
	}
	log.Println("created new user artifact directory", dir)
	return &apiv1.UploadDirectory{Name: filepath.Base(dir)}, nil
}

func (m *UserArtifactsManagerImpl) lock(dir, name, task string) (bool, error) {
	ctx, cancel := context.WithCancel(context.TODO())
	defer cancel()
	lockDirPath := filepath.Join(m.RootDir, lockDir)
	lockPath := filepath.Join(lockDirPath, fmt.Sprintf("%s-%s-%s.lock", dir, name, task))
	m.locks[lockPath] = flock.New(lockPath)
	locked, err := m.locks[lockPath].TryLockContext(ctx, time.Second)
	if err != nil {
		return false, fmt.Errorf("failed acquiring lock at %q: %w", lockPath, err)
	}
	if !locked {
		return false, fmt.Errorf("failed acquiring lock at %q", lockPath)
	}

	completedPath := filepath.Join(lockDirPath, fmt.Sprintf("%s-%s-%s.completed", dir, name, task))
	exists, err := fileExist(completedPath)
	if err != nil {
		return false, fmt.Errorf("failed to check existence of file at %q: %w", completedPath, err)
	}
	return exists, nil
}

func (m *UserArtifactsManagerImpl) unlock(dir, name, task string) error {
	lockDirPath := filepath.Join(m.RootDir, lockDir)
	lockPath := filepath.Join(lockDirPath, fmt.Sprintf("%s-%s-%s.lock", dir, name, task))
	if m.locks[lockPath] == nil {
		return fmt.Errorf("lock at %q wasn't defined", lockPath)
	}
	if !m.locks[lockPath].Locked() {
		return fmt.Errorf("lock at %q was already released", lockPath)
	}
	completedPath := filepath.Join(lockDirPath, fmt.Sprintf("%s-%s-%s.completed", dir, name, task))
	file, err := os.Create(completedPath)
	if err != nil {
		return fmt.Errorf("failed to create file at %q: %w", completedPath, err)
	}
	defer file.Close()
	m.locks[lockPath].Unlock()
	m.locks[lockPath] = nil
	return nil
}

func (m *UserArtifactsManagerImpl) LockFile(dir, name string) (*apiv1.LockFileResponse, error) {
	completed, err := m.lock(dir, name, "upload")
	if err != nil {
		return nil, err
	}
	return &apiv1.LockFileResponse{UploadCompleted: completed}, nil
}

func (m *UserArtifactsManagerImpl) UnlockFile(dir, name string) (*apiv1.LockFileResponse, error) {
	err := m.unlock(dir, name, "upload")
	if err != nil {
		return nil, err
	}
	return &apiv1.LockFileResponse{UploadCompleted: true}, nil
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
		if entry.IsDir() && entry.Name() != lockDir {
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
	filename := filepath.Join(dir, chunk.Name)
	if err := createUAFile(filename, m.Owner); err != nil {
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
	if _, err := f.Seek(int64(chunk.ChunkNumber-1)*chunk.ChunkSizeBytes, 0); err != nil {
		return err
	}
	if _, err = io.Copy(f, chunk.File); err != nil {
		return err
	}
	return nil
}

func (m *UserArtifactsManagerImpl) ExtractArtifact(dir, name string) error {
	dirPath := filepath.Join(m.RootDir, dir)
	if ok, err := fileExist(dirPath); err != nil {
		return err
	} else if !ok {
		return operator.NewBadRequestError(fmt.Sprintf("directory %q does not exist", dirPath), nil)
	}
	filename := filepath.Join(dirPath, name)
	if ok, err := fileExist(filename); err != nil {
		return err
	} else if !ok {
		return operator.NewBadRequestError(fmt.Sprintf("artifact %q does not exist", name), nil)
	}
	completed, err := m.lock(dir, name, "extract")
	if err != nil {
		return err
	}
	if !completed {
		if strings.HasSuffix(filename, ".tar.gz") {
			if err := Untar(dirPath, filename, m.Owner); err != nil {
				return fmt.Errorf("failed extracting %q: %w", name, err)
			}
		} else if strings.HasSuffix(filename, ".zip") {
			if err := Unzip(dirPath, filename, m.Owner); err != nil {
				return fmt.Errorf("failed extracting %q: %w", name, err)
			}
		} else {
			return operator.NewBadRequestError(fmt.Sprintf("unsupported extension: %q", name), nil)
		}
	}
	return m.unlock(dir, name, "extract")
}

func Untar(dst string, src string, owner *user.User) error {
	ctx := hoexec.NewAsUserExecContext(exec.CommandContext, owner)
	_, err := hoexec.Exec(ctx, "tar", "-xf", src, "-C", dst)
	if err != nil {
		return err
	}
	return nil
}

func Unzip(dstDir string, src string, owner *user.User) error {
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
		if err := createUAFile(dst, owner); err != nil {
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

func createNewUADir(parent string, dir string, owner *user.User) (string, error) {
	ctx := hoexec.NewAsUserExecContext(exec.CommandContext, owner)
	var name string
	if dir == "" {
		stdout, err := hoexec.Exec(ctx, "mktemp", "--directory", "-p", parent)
		if err != nil {
			return "", err
		}
		name = strings.TrimRight(stdout, "\n")
	} else {
		name = filepath.Join(parent, dir)
		if _, err := hoexec.Exec(ctx, "mkdir", "-p", name); err != nil {
			return "", err
		}
	}
	// Sets permission regardless of umask.
	if _, err := hoexec.Exec(ctx, "chmod", "u=rwx,g=rwx,o=r", name); err != nil {
		return "", err
	}
	return name, nil
}

func createUAFile(filename string, owner *user.User) error {
	ctx := hoexec.NewAsUserExecContext(exec.CommandContext, owner)
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
