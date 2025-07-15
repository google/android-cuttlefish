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
	"crypto/sha256"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"syscall"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	"github.com/google/android-cuttlefish/frontend/src/liboperator/operator"
	"github.com/google/btree"
	"github.com/google/uuid"
)

// Resolves the user artifacts full directory.
type UserArtifactsDirResolver interface {
	// Retrieve the full path where the updated artifact located
	UpdatedArtifactPath(checksum string) string
	// Retrieve the full path where the extracted artifact located
	ExtractedArtifactPath(checksum string) string

	// Given a directory name returns its full path.
	// Deprecated: use `UpdatedArtifactPath` instead
	GetDirPath(name string) string
}

type UserArtifactChunk struct {
	Name          string
	File          io.Reader
	OffsetBytes   int64
	SizeBytes     int64
	FileSizeBytes int64
}

// Abstraction for managing user artifacts for launching CVDs.
type UserArtifactsManager interface {
	// Update artifact with the passed chunk
	UpdateArtifact(checksum string, chunk UserArtifactChunk) error
	// Stat artifact whether artifact with given checksum exists or not.
	StatArtifact(checksum string) (*apiv1.StatArtifactResponse, error)
	// Extract artifact with the given checksum
	ExtractArtifact(checksum string) error

	UserArtifactsDirResolver
	// Creates a new directory for uploading user artifacts in the future.
	// Deprecated: use `UpdateArtifact` instead
	NewDir() (*apiv1.UploadDirectory, error)
	// List existing directories
	// Deprecated: use `UpdateArtifact` instead
	ListDirs() (*apiv1.ListUploadDirectoriesResponse, error)
	// Update artifact with the passed chunk
	// Deprecated: use `UpdateArtifact` instead
	UpdateArtifactWithDir(dir string, chunk UserArtifactChunk) error
	// Extract artifact
	// Deprecated: use `ExtractArtifact` instead
	ExtractArtifactWithDir(dir, name string) error
}

// Options for creating instances of UserArtifactsManager implementations.
type UserArtifactsManagerOpts struct {
	// Root directory for legacy APIs treating user artifacts.
	LegacyRootDir string
	// Root directory for storing user artifacts. After Host Orchestrator moves user artifacts from
	// the working directory into here, it becomes immutable unless introducing any replacement
	// algorithm(e.g. LRU) on RootDir to manage the storage.
	RootDir string
}

// An implementation of the UserArtifactsManager interface.
type UserArtifactsManagerImpl struct {
	UserArtifactsManagerOpts
	WorkDir     string
	mutexes     sync.Map
	chunkStates sync.Map
}

// Creates a new instance of UserArtifactsManagerImpl.
func NewUserArtifactsManagerImpl(opts UserArtifactsManagerOpts) (*UserArtifactsManagerImpl, error) {
	if err := createDir(opts.RootDir); err != nil {
		return nil, fmt.Errorf("failed to create root directory for storing user artifacts: %w", err)
	}
	commonWorkDir := filepath.Join(opts.RootDir, "working")
	if err := createDir(commonWorkDir); err != nil {
		return nil, fmt.Errorf("failed to create working directory for all Host Orchestrators: %w", err)
	}
	uuidWorkDir := filepath.Join(commonWorkDir, uuid.New().String())
	if err := os.RemoveAll(uuidWorkDir); err != nil {
		return nil, fmt.Errorf("failed to clean working directory for this Host Orchestrator: %w", err)
	}
	if err := createDir(uuidWorkDir); err != nil {
		return nil, fmt.Errorf("failed to create working directory for this Host Orchestrator: %w", err)
	}
	return &UserArtifactsManagerImpl{
		UserArtifactsManagerOpts: opts,
		WorkDir:                  uuidWorkDir,
	}, nil
}

func (m *UserArtifactsManagerImpl) NewDir() (*apiv1.UploadDirectory, error) {
	if err := createDir(m.LegacyRootDir); err != nil {
		return nil, err
	}
	dir, err := ioutil.TempDir(m.LegacyRootDir, "")
	if err != nil {
		return nil, err
	}
	if err := os.Chmod(dir, 0755); err != nil {
		return nil, fmt.Errorf("failed to grant read permission at %q: %w", dir, err)
	}
	log.Println("created new user artifact directory", dir)
	return &apiv1.UploadDirectory{Name: filepath.Base(dir)}, nil
}

func (m *UserArtifactsManagerImpl) ListDirs() (*apiv1.ListUploadDirectoriesResponse, error) {
	exist, err := fileExist(m.LegacyRootDir)
	if err != nil {
		return nil, err
	}
	if !exist {
		return &apiv1.ListUploadDirectoriesResponse{Items: make([]*apiv1.UploadDirectory, 0)}, nil
	}
	entries, err := ioutil.ReadDir(m.LegacyRootDir)
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

func (m *UserArtifactsManagerImpl) UpdatedArtifactPath(checksum string) string {
	return filepath.Join(m.RootDir, checksum)
}

func (m *UserArtifactsManagerImpl) ExtractedArtifactPath(checksum string) string {
	return filepath.Join(m.RootDir, fmt.Sprintf("%s_extracted", checksum))
}

func (m *UserArtifactsManagerImpl) GetDirPath(name string) string {
	return filepath.Join(m.LegacyRootDir, name)
}

func (m *UserArtifactsManagerImpl) UpdateArtifact(checksum string, chunk UserArtifactChunk) error {
	if chunk.OffsetBytes < 0 {
		return operator.NewBadRequestError(fmt.Sprintf("invalid value (chunk_offset:%d)", chunk.OffsetBytes), nil)
	}
	if chunk.SizeBytes < 0 {
		return fmt.Errorf("invalid value (chunk_size:%d)", chunk.SizeBytes)
	}
	if chunk.FileSizeBytes < 0 {
		return operator.NewBadRequestError(fmt.Sprintf("invalid value (file_size:%d)", chunk.FileSizeBytes), nil)
	}
	if chunk.OffsetBytes+chunk.SizeBytes > chunk.FileSizeBytes {
		return operator.NewBadRequestError(fmt.Sprintf("invalid value pair (chunk_offset:%d, chunk_size:%d, file_size:%d)", chunk.OffsetBytes, chunk.SizeBytes, chunk.FileSizeBytes), nil)
	}
	mayMoveArtifact, err := m.writeChunkAndUpdateState(checksum, chunk)
	if err != nil {
		if _, ok := err.(*operator.AppError); ok {
			return err
		}
		return fmt.Errorf("failed to update chunk: %w", err)
	}
	if mayMoveArtifact {
		if err := m.moveArtifactIfNeeded(checksum, chunk); err != nil {
			return fmt.Errorf("failed to move the user artifact from working directory: %w", err)
		}
	}
	return nil
}

func (m *UserArtifactsManagerImpl) UpdateArtifactWithDir(dir string, chunk UserArtifactChunk) error {
	dir = filepath.Join(m.LegacyRootDir, dir)
	if ok, err := fileExist(dir); err != nil {
		return err
	} else if !ok {
		return operator.NewBadRequestError("upload directory %q does not exist", err)
	}
	filename := filepath.Join(dir, chunk.Name)
	if err := writeChunk(filename, chunk); err != nil {
		return err
	}
	return nil
}

func (m *UserArtifactsManagerImpl) StatArtifact(checksum string) (*apiv1.StatArtifactResponse, error) {
	if exists, err := dirExists(m.UpdatedArtifactPath(checksum)); err != nil {
		return nil, fmt.Errorf("failed to check existence of directory: %w", err)
	} else if !exists {
		return nil, operator.NewNotFoundError(fmt.Sprintf("user artifact(checksum:%q) not found", checksum), nil)
	}
	return &apiv1.StatArtifactResponse{}, nil
}

func (m *UserArtifactsManagerImpl) ExtractArtifact(checksum string) error {
	dir := m.ExtractedArtifactPath(checksum)
	if exists, err := dirExists(dir); err != nil {
		return fmt.Errorf("failed to check existence of directory: %w", err)
	} else if exists {
		return operator.NewConflictError(fmt.Sprintf("user artifact(checksum:%q) already extracted", checksum), nil)
	}
	file, err := m.getFilePath(checksum)
	if err != nil {
		return err
	}
	workDir, err := ioutil.TempDir(m.WorkDir, "")
	if err != nil {
		return err
	}
	if err := os.Chmod(workDir, 0755); err != nil {
		return fmt.Errorf("failed to grant read permission at %q: %w", dir, err)
	}
	if err := extractFile(workDir, file); err != nil {
		return err
	}
	if err := os.Rename(workDir, dir); err != nil {
		return fmt.Errorf("failed to move the user artifact: %w", err)
	}
	return nil
}

func (m *UserArtifactsManagerImpl) getRWMutex(checksum string) *sync.RWMutex {
	mu, _ := m.mutexes.LoadOrStore(checksum, &sync.RWMutex{})
	return mu.(*sync.RWMutex)
}

func (m *UserArtifactsManagerImpl) getChunkState(checksum string, fileSize int64) *chunkState {
	cs, _ := m.chunkStates.LoadOrStore(checksum, NewChunkState(fileSize))
	return cs.(*chunkState)
}

func dirExists(dir string) (bool, error) {
	if info, err := os.Stat(dir); os.IsNotExist(err) {
		return false, nil
	} else if err != nil {
		return false, err
	} else {
		return info.IsDir(), nil
	}
}

func writeChunk(filename string, chunk UserArtifactChunk) error {
	f, err := os.OpenFile(filename, os.O_WRONLY|os.O_CREATE, 0755)
	if err != nil {
		return fmt.Errorf("failed to open or create working file: %w", err)
	}
	defer f.Close()
	if _, err := f.Seek(chunk.OffsetBytes, io.SeekStart); err != nil {
		return fmt.Errorf("failed to seek offset of working file: %w", err)
	}
	if _, err := io.Copy(f, chunk.File); err != nil {
		return fmt.Errorf("failed to copy chunk into working file: %w", err)
	}
	return nil
}

// Calculating the checksum of the user artifact is a heavy task. Instead, it records the state
// of updated chunks to know whether it's ready to calculate checksum and move the user artifact or
// not. This function returns the boolean value as true if the user artifact is fully updated so it
// needs to calculate checksum later.
func (m *UserArtifactsManagerImpl) writeChunkAndUpdateState(checksum string, chunk UserArtifactChunk) (bool, error) {
	mu := m.getRWMutex(checksum)
	// Reason for acquiring read lock is to allow updating multiple chunks concurrently, but to
	// restrict validating checksum or moving artifact into different directory when one or more
	// chunks are being updated.
	mu.RLock()
	defer mu.RUnlock()
	if exists, err := dirExists(m.UpdatedArtifactPath(checksum)); err != nil {
		return false, fmt.Errorf("failed to check existence of directory: %w", err)
	} else if exists {
		return false, operator.NewConflictError(fmt.Sprintf("user artifact(checksum:%q) already exists", checksum), nil)
	}
	workDir := filepath.Join(m.WorkDir, checksum)
	if err := createDir(workDir); err != nil {
		return false, fmt.Errorf("failed to create working directory: %w", err)
	}
	if err := writeChunk(filepath.Join(workDir, chunk.Name), chunk); err != nil {
		return false, err
	}
	cs := m.getChunkState(checksum, chunk.FileSizeBytes)
	cs.Update(chunk.OffsetBytes, chunk.OffsetBytes+chunk.SizeBytes)
	return cs.IsCompleted(), nil
}

func (m *UserArtifactsManagerImpl) validateChecksum(checksum string, chunk UserArtifactChunk) (bool, error) {
	workFile := filepath.Join(m.WorkDir, checksum, chunk.Name)
	f, err := os.Open(workFile)
	if err != nil {
		return false, fmt.Errorf("failed to open working file: %w", err)
	}
	defer f.Close()
	if info, err := f.Stat(); err != nil {
		return false, err
	} else if chunk.FileSizeBytes != info.Size() {
		return false, nil
	}
	h := sha256.New()
	if _, err := io.Copy(h, f); err != nil {
		return false, err
	}
	return checksum == fmt.Sprintf("%x", h.Sum(nil)), nil
}

func (m *UserArtifactsManagerImpl) moveArtifactIfNeeded(checksum string, chunk UserArtifactChunk) error {
	mu := m.getRWMutex(checksum)
	// Reason for acquiring write lock is to restrict writing new chunks while validating checksum
	// or moving artifact.
	mu.Lock()
	defer mu.Unlock()
	dir := m.UpdatedArtifactPath(checksum)
	if exists, err := dirExists(dir); err != nil {
		return fmt.Errorf("failed to check existence of directory: %w", err)
	} else if exists {
		return nil
	}
	if matches, err := m.validateChecksum(checksum, chunk); err != nil {
		return fmt.Errorf("failed to validate checksum of the user artifact: %w", err)
	} else if !matches {
		// Only if the checksum matches, it's ready to move the user artifact from working
		// directory.
		return nil
	}
	workDir := filepath.Join(m.WorkDir, checksum)
	if err := os.Rename(workDir, dir); err != nil {
		return fmt.Errorf("failed to move the user artifact: %w", err)
	}
	m.chunkStates.Delete(checksum)
	return nil
}

func (m *UserArtifactsManagerImpl) getFilePath(checksum string) (string, error) {
	dir := m.UpdatedArtifactPath(checksum)
	if exists, err := dirExists(dir); err != nil {
		return "", fmt.Errorf("failed to check existence of directory: %w", err)
	} else if !exists {
		return "", operator.NewNotFoundError(fmt.Sprintf("user artifact(checksum:%q) not found", checksum), nil)
	}
	if entries, err := ioutil.ReadDir(dir); err != nil {
		return "", fmt.Errorf("failed to read directory where user artifact located: %w", err)
	} else if len(entries) != 1 || entries[0].IsDir() {
		return "", fmt.Errorf("directory where user artifact located should contain a single file only")
	} else {
		return filepath.Join(dir, entries[0].Name()), nil
	}
}

func extractFile(dst string, src string) error {
	if strings.HasSuffix(src, ".tar.gz") {
		if err := untar(dst, src); err != nil {
			return fmt.Errorf("failed to extract tar.gz file: %w", err)
		}
	} else if strings.HasSuffix(src, ".zip") {
		if err := unzip(dst, src); err != nil {
			return fmt.Errorf("failed to extract zip file: %w", err)
		}
	} else {
		return operator.NewBadRequestError(fmt.Sprintf("unsupported extension: %q", src), nil)
	}
	return nil
}

func (m *UserArtifactsManagerImpl) ExtractArtifactWithDir(dir, name string) error {
	dir = filepath.Join(m.LegacyRootDir, dir)
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
	return extractFile(dir, filename)
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

func unzip(dstDir string, src string) error {
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
		dstFile, err := os.OpenFile(dst, os.O_WRONLY|os.O_CREATE, 0755)
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

// Structure for managing the state of updated chunk for efficiently knowing whether the user
// artifact may need to calculate hash sum or not.
type chunkState struct {
	fileSize int64
	// Items are stored with a sequence of alternating true/false isUpdated field and increasing
	// offset field.
	items *btree.BTree
	mutex sync.RWMutex
}

type chunkStateItem struct {
	// Starting byte offset of the continuous range having same state whether updated or not.
	offset int64
	// State description whether given byte offset of the user artifact is updated or not.
	isUpdated bool
}

func (i chunkStateItem) Less(item btree.Item) bool {
	return i.offset < item.(chunkStateItem).offset
}

func NewChunkState(fileSize int64) *chunkState {
	cs := chunkState{
		fileSize: fileSize,
		items:    btree.New(2),
		mutex:    sync.RWMutex{},
	}
	return &cs
}

func (cs *chunkState) getItemOrPrev(offset int64) *chunkStateItem {
	var record *chunkStateItem
	cs.items.DescendLessOrEqual(chunkStateItem{offset: offset}, func(item btree.Item) bool {
		entry := item.(chunkStateItem)
		record = &entry
		return false
	})
	return record
}

func (cs *chunkState) getItemOrNext(offset int64) *chunkStateItem {
	var record *chunkStateItem
	cs.items.AscendGreaterOrEqual(chunkStateItem{offset: offset}, func(item btree.Item) bool {
		entry := item.(chunkStateItem)
		record = &entry
		return false
	})
	return record
}

func (cs *chunkState) Update(start int64, end int64) error {
	if start < 0 {
		return fmt.Errorf("invalid start offset of the range")
	}
	if end > cs.fileSize {
		return fmt.Errorf("invalid end offset of the range")
	}
	if start >= end {
		return fmt.Errorf("start offset should be less than end offset")
	}
	cs.mutex.Lock()
	defer cs.mutex.Unlock()

	// Remove all current state between the start offset and the end offset.
	for item := cs.getItemOrNext(start); item != nil && item.offset <= end; item = cs.getItemOrNext(start) {
		cs.items.Delete(*item)
	}
	// State of the start offset is updated according to the state of the previous item.
	if prev := cs.getItemOrPrev(start); prev == nil || !prev.isUpdated {
		cs.items.ReplaceOrInsert(chunkStateItem{offset: start, isUpdated: true})
	}
	// State of the end offset is updated according to the state of the next item.
	if next := cs.getItemOrNext(end); next == nil || next.isUpdated {
		cs.items.ReplaceOrInsert(chunkStateItem{offset: end, isUpdated: false})
	}
	return nil
}

func (cs *chunkState) IsCompleted() bool {
	cs.mutex.RLock()
	defer cs.mutex.RUnlock()
	if cs.items.Len() != 2 {
		return false
	}
	first := chunkStateItem{offset: 0, isUpdated: true}
	last := chunkStateItem{offset: cs.fileSize, isUpdated: false}
	return cs.items.Min() == first && cs.items.Max() == last
}
