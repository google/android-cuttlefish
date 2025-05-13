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
	"os"
	"path/filepath"
	"strings"
	"sync"
	"testing"

	orchtesting "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/testing"
	"github.com/google/go-cmp/cmp"
)

const (
	testFileName = "foo.txt"
	testFileData = "abcdefghijklmnopqrstuvwxyz"
)

func getSha1Hash() string {
	return fmt.Sprintf("%x", sha1.Sum([]byte(testFileData)))
}

func getSingleChunk() UserArtifactChunkV1 {
	return UserArtifactChunkV1{
		Name:     testFileName,
		Offset:   0,
		Size:     int64(len(testFileData)),
		FileSize: int64(len(testFileData)),
		File:     strings.NewReader(testFileData),
		Hash:     getSha1Hash(),
	}
}

func getMultipleChunk() []UserArtifactChunkV1 {
	hash := getSha1Hash()
	chunks := []UserArtifactChunkV1{}
	for idxStart := 0; idxStart < len(testFileData); {
		idxEnd := idxStart*2 + 1
		if idxEnd > len(testFileData) {
			idxEnd = len(testFileData)
		}
		chunks = append(chunks, UserArtifactChunkV1{
			Name:     testFileName,
			Offset:   int64(idxStart),
			Size:     int64(idxEnd - idxStart),
			FileSize: int64(len(testFileData)),
			File:     strings.NewReader(testFileData[idxStart:idxEnd]),
			Hash:     hash,
		})
		idxStart = idxEnd
	}
	return chunks
}

func TestUpdateArtifactWithSingleChunkSucceeds(t *testing.T) {
	rootDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, rootDir)
	rootWorkDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, rootWorkDir)
	opts := UserArtifactsManagerV1Opts{RootDir: rootDir, RootWorkDir: rootWorkDir}
	uam := NewUserArtifactsManagerV1Impl(opts)

	if err := uam.UpdateArtifact(getSingleChunk()); err != nil {
		t.Fatal(err)
	}
	b, err := os.ReadFile(filepath.Join(uam.GetDirPath(getSha1Hash()), testFileName))
	if err != nil {
		t.Fatal(err)
	}
	if diff := cmp.Diff(testFileData, string(b)); diff != "" {
		t.Fatalf("artifact content mismatch (-want +got):\n%s", diff)
	}
}

func TestUpdateArtifactWithNegativeChunkOffsetFails(t *testing.T) {
	rootDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, rootDir)
	rootWorkDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, rootWorkDir)
	opts := UserArtifactsManagerV1Opts{RootDir: rootDir, RootWorkDir: rootWorkDir}
	uam := NewUserArtifactsManagerV1Impl(opts)

	chunk := getSingleChunk()
	chunk.Offset = -1
	if err := uam.UpdateArtifact(chunk); err == nil {
		t.Fatal("Expected an error")
	}
}

func TestUpdateArtifactWithNegativeChunkSizeFails(t *testing.T) {
	rootDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, rootDir)
	rootWorkDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, rootWorkDir)
	opts := UserArtifactsManagerV1Opts{RootDir: rootDir, RootWorkDir: rootWorkDir}
	uam := NewUserArtifactsManagerV1Impl(opts)

	chunk := getSingleChunk()
	chunk.Size = -1
	if err := uam.UpdateArtifact(chunk); err == nil {
		t.Fatal("Expected an error")
	}
}

func TestUpdateArtifactWithNegativeFileSizeFails(t *testing.T) {
	rootDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, rootDir)
	rootWorkDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, rootWorkDir)
	opts := UserArtifactsManagerV1Opts{RootDir: rootDir, RootWorkDir: rootWorkDir}
	uam := NewUserArtifactsManagerV1Impl(opts)

	chunk := getSingleChunk()
	chunk.FileSize = -1
	if err := uam.UpdateArtifact(chunk); err == nil {
		t.Fatal("Expected an error")
	}
}

func TestUpdateArtifactWithChunkSizeOverflowFails(t *testing.T) {
	rootDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, rootDir)
	rootWorkDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, rootWorkDir)
	opts := UserArtifactsManagerV1Opts{RootDir: rootDir, RootWorkDir: rootWorkDir}
	uam := NewUserArtifactsManagerV1Impl(opts)

	chunk := getSingleChunk()
	chunk.Size += 1
	if err := uam.UpdateArtifact(chunk); err == nil {
		t.Fatal("Expected an error")
	}
}

func TestUpdateArtifactAfterArtifactIsFullyUploadedFails(t *testing.T) {
	rootDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, rootDir)
	rootWorkDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, rootWorkDir)
	opts := UserArtifactsManagerV1Opts{RootDir: rootDir, RootWorkDir: rootWorkDir}
	uam := NewUserArtifactsManagerV1Impl(opts)

	chunk := getSingleChunk()
	if err := uam.UpdateArtifact(chunk); err != nil {
		t.Fatal(err)
	}
	if err := uam.UpdateArtifact(chunk); err == nil {
		t.Fatal(err)
	}
}

func TestUpdateArtifactWithMultipleSerialChunkSucceeds(t *testing.T) {
	rootDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, rootDir)
	rootWorkDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, rootWorkDir)
	opts := UserArtifactsManagerV1Opts{RootDir: rootDir, RootWorkDir: rootWorkDir}
	uam := NewUserArtifactsManagerV1Impl(opts)

	chunks := getMultipleChunk()
	for _, chunk := range chunks {
		if err := uam.UpdateArtifact(chunk); err != nil {
			t.Fatal(err)
		}
	}
	b, err := os.ReadFile(filepath.Join(uam.GetDirPath(getSha1Hash()), testFileName))
	if err != nil {
		t.Fatal(err)
	}
	if diff := cmp.Diff(testFileData, string(b)); diff != "" {
		t.Fatalf("artifact content mismatch (-want +got):\n%s", diff)
	}
}

func TestUpdateArtifactWithMultipleParallelChunkSucceeds(t *testing.T) {
	rootDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, rootDir)
	rootWorkDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, rootWorkDir)
	opts := UserArtifactsManagerV1Opts{RootDir: rootDir, RootWorkDir: rootWorkDir}
	uam := NewUserArtifactsManagerV1Impl(opts)

	chunks := getMultipleChunk()
	wg := sync.WaitGroup{}
	wg.Add(len(chunks))
	for _, chunk := range chunks {
		go func(chunk UserArtifactChunkV1) {
			defer wg.Done()
			if err := uam.UpdateArtifact(chunk); err != nil {
				t.Error(err)
			}
		}(chunk)
	}
	wg.Wait()
	b, err := os.ReadFile(filepath.Join(uam.GetDirPath(getSha1Hash()), testFileName))
	if err != nil {
		t.Fatal(err)
	}
	if diff := cmp.Diff(testFileData, string(b)); diff != "" {
		t.Fatalf("artifact content mismatch (-want +got):\n%s", diff)
	}
}
