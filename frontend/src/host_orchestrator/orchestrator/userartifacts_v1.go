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
	"crypto/sha1"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"sync"
	"syscall"

	"github.com/google/android-cuttlefish/frontend/src/liboperator/operator"
)

type UserArtifactChunkV1 struct {
	// Name of the file
	Name string
	// Contains chunk data
	File io.Reader
	// SHA1 hash of the whole file data
	Hash string
	// Byte offset of the chunk
	Offset int64
	// Size of the chunk
	Size int64
	// Size of the whole file
	FileSize int64
}

// Abstraction for managing user artifacts for launching CVDs. This is for replacing
// UserArtifactManager, the legacy manager for the same role.
type UserArtifactsManagerV1 interface {
	UserArtifactsDirResolver
	// Update artifact with the passed chunk
	UpdateArtifact(chunk UserArtifactChunkV1) error
}

type UserArtifactsManagerV1Opts struct {
	// Root Directory for storing user artifacts. After Host Orchestrator moves user artifacts into
	// RootDir, it becomes immutable unless introducing any replacement algorithm(e.g. LRU) on
	// RootDir to manage the storage.
	RootDir string
	// Working directory for storing user artifacts for a while. After all tasks that updating all
	// chunks are completed, the artifact stored under RootWorkDir is moved into RootDir.
	RootWorkDir string
}

// An implementation of the UserArtifactsManagerV1 interface.
type UserArtifactsManagerV1Impl struct {
	UserArtifactsManagerV1Opts
	writeChunkWaitGroups sync.Map
	moveArtifactMutexes  sync.Map
	chunkStates          sync.Map
}

// Creates a new instance of UserArtifactsManagerV1Impl.
func NewUserArtifactsManagerV1Impl(opts UserArtifactsManagerV1Opts) *UserArtifactsManagerV1Impl {
	return &UserArtifactsManagerV1Impl{
		UserArtifactsManagerV1Opts: opts,
	}
}

func (m *UserArtifactsManagerV1Impl) GetDirPath(name string) string {
	return filepath.Join(m.RootDir, name)
}

func (m *UserArtifactsManagerV1Impl) UpdateArtifact(chunk UserArtifactChunkV1) error {
	if chunk.Offset < 0 {
		return operator.NewBadRequestError(fmt.Sprintf("invalid value (chunk_offset:%d)", chunk.Offset), nil)
	}
	if chunk.Size < 0 {
		return fmt.Errorf("invalid value (chunk_size:%d)", chunk.Size)
	}
	if chunk.FileSize < 0 {
		return operator.NewBadRequestError(fmt.Sprintf("invalid value (file_size:%d)", chunk.FileSize), nil)
	}
	if chunk.Offset+chunk.Size > chunk.FileSize {
		return operator.NewBadRequestError(fmt.Sprintf("invalid value pair (chunk_offset:%d, chunk_size:%d, file_size:%d)", chunk.Offset, chunk.Size, chunk.FileSize), nil)
	}
	if err := m.writeChunk(chunk); err != nil {
		return fmt.Errorf("failed to update chunk: %w", err)
	}
	if err := m.moveArtifactIfNeeds(chunk); err != nil {
		return fmt.Errorf("failed to move user artifact from working directory: %w", err)
	}
	return nil
}

func (m *UserArtifactsManagerV1Impl) getWriteChunkWaitGroup(hash string) *sync.WaitGroup {
	wg, _ := m.writeChunkWaitGroups.LoadOrStore(hash, &sync.WaitGroup{})
	return wg.(*sync.WaitGroup)
}

func (m *UserArtifactsManagerV1Impl) getMoveArtifactMutex(hash string) *sync.RWMutex {
	mu, _ := m.moveArtifactMutexes.LoadOrStore(hash, &sync.RWMutex{})
	return mu.(*sync.RWMutex)
}

func (m *UserArtifactsManagerV1Impl) dirExists(hash string) (bool, error) {
	if info, err := os.Stat(filepath.Join(m.RootDir, hash)); os.IsNotExist(err) {
		return false, nil
	} else if err != nil {
		return false, err
	} else {
		return info.IsDir(), nil
	}
}

func (m *UserArtifactsManagerV1Impl) writeChunk(chunk UserArtifactChunkV1) error {
	mu := m.getMoveArtifactMutex(chunk.Hash)
	mu.Lock()
	wg := m.getWriteChunkWaitGroup(chunk.Hash)
	wg.Add(1)
	mu.Unlock()
	defer wg.Done()
	if dirExists, err := m.dirExists(chunk.Hash); err != nil {
		return fmt.Errorf("failed to check existence of directory: %w", err)
	} else if dirExists {
		return operator.NewConflictError(fmt.Sprintf("user artifact(hash:%q) already exists", chunk.Hash), nil)
	}
	workDir := filepath.Join(m.RootWorkDir, chunk.Hash)
	if err := createDir(workDir); err != nil {
		return fmt.Errorf("failed to create working directory for uploading user artifact: %w", err)
	}
	workFile := filepath.Join(workDir, chunk.Name)
	f, err := os.OpenFile(workFile, os.O_WRONLY|os.O_CREATE, 0664)
	if err != nil {
		return fmt.Errorf("failed to create or open working file for uploading user artifact: %w", err)
	}
	defer f.Close()
	if _, err := f.Seek(chunk.Offset, io.SeekStart); err != nil {
		return fmt.Errorf("failed to seek offset of working file: %w", err)
	}
	if _, err := io.Copy(f, chunk.File); err != nil {
		return fmt.Errorf("failed to copy chunk into working file: %w", err)
	}
	return nil
}

func (m *UserArtifactsManagerV1Impl) checkHash(chunk UserArtifactChunkV1) (bool, error) {
	workFile := filepath.Join(m.RootWorkDir, chunk.Hash, chunk.Name)
	f, err := os.Open(workFile)
	if err != nil {
		return false, fmt.Errorf("failed to open working file: %w", err)
	}
	defer f.Close()
	if info, err := f.Stat(); err != nil {
		return false, err
	} else if chunk.FileSize != info.Size() {
		return false, nil
	}
	h := sha1.New()
	if _, err := io.Copy(h, f); err != nil {
		return false, err
	}
	return chunk.Hash == fmt.Sprintf("%x", h.Sum(nil)), nil
}

func (m *UserArtifactsManagerV1Impl) moveArtifactIfNeeds(chunk UserArtifactChunkV1) error {
	m.getWriteChunkWaitGroup(chunk.Hash).Wait()
	mu := m.getMoveArtifactMutex(chunk.Hash)
	mu.Lock()
	defer mu.Unlock()
	if dirExists, err := m.dirExists(chunk.Hash); err != nil {
		return fmt.Errorf("failed to check existence of directory: %w", err)
	} else if dirExists {
		return nil
	}
	if matches, err := m.checkHash(chunk); err != nil {
		return fmt.Errorf("failed to check whether all chunks are updated or not: %w", err)
	} else if !matches {
		// Only if the hash matches, it's ready to move the user artifact from working directory.
		return nil
	}
	workDir := filepath.Join(m.RootWorkDir, chunk.Hash)
	dir := filepath.Join(m.RootDir, chunk.Hash)
	if err := os.Rename(workDir, dir); err != nil {
		if linkErr, ok := err.(*os.LinkError); ok {
			if linkErr.Err == syscall.EXDEV {
				return fmt.Errorf("trying to move the user artifact across different file system")
			} else {
				return linkErr
			}
		} else {
			return err
		}
	}
	return nil
}
