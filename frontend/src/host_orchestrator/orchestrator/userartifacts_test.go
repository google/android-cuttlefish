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
	"crypto/sha256"
	"errors"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"testing"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	orchtesting "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/testing"

	"github.com/google/go-cmp/cmp"
)

const (
	testFileName = "foo.txt"
	testFileData = "abcdefghijklmnopqrstuvwxyz"
)

func TestNewDir(t *testing.T) {
	legacyRootDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, legacyRootDir)
	rootDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, rootDir)
	opts := UserArtifactsManagerOpts{LegacyRootDir: legacyRootDir, RootDir: rootDir}
	am, err := NewUserArtifactsManagerImpl(opts)
	if err != nil {
		t.Fatal(err)
	}

	upDir, err := am.NewDir()

	if err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(filepath.Join(am.LegacyRootDir, upDir.Name)); errors.Is(err, os.ErrNotExist) {
		t.Errorf("upload dir %q does not exist", upDir.Name)
	}
}

func TestListDirsAndNoDirHasBeenCreated(t *testing.T) {
	legacyRootDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, legacyRootDir)
	rootDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, rootDir)
	opts := UserArtifactsManagerOpts{LegacyRootDir: legacyRootDir, RootDir: rootDir}
	am, err := NewUserArtifactsManagerImpl(opts)
	if err != nil {
		t.Fatal(err)
	}

	res, _ := am.ListDirs()

	exp := &apiv1.ListUploadDirectoriesResponse{Items: make([]*apiv1.UploadDirectory, 0)}
	if diff := cmp.Diff(exp, res); diff != "" {
		t.Errorf("response mismatch (-want +got):\n%s", diff)
	}
}

func TestListTokens(t *testing.T) {
	legacyRootDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, legacyRootDir)
	rootDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, rootDir)
	opts := UserArtifactsManagerOpts{LegacyRootDir: legacyRootDir, RootDir: rootDir}
	am, err := NewUserArtifactsManagerImpl(opts)
	if err != nil {
		t.Fatal(err)
	}
	am.NewDir()
	am.NewDir()

	res, err := am.ListDirs()

	if err != nil {
		t.Fatal(err)
	}
	entries, err := ioutil.ReadDir(legacyRootDir)
	if err != nil {
		t.Fatal(err)
	}
	exp := &apiv1.ListUploadDirectoriesResponse{Items: []*apiv1.UploadDirectory{}}
	for _, e := range entries {
		exp.Items = append(exp.Items, &apiv1.UploadDirectory{Name: e.Name()})
	}
	if diff := cmp.Diff(2, len(res.Items)); diff != "" {
		t.Errorf("response mismatch (-want +got):\n%s", diff)
	}
}

func TestUpdateArtifactWithDirFailsWhenDirectoryDoesNotExist(t *testing.T) {
	legacyRootDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, legacyRootDir)
	rootDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, rootDir)
	opts := UserArtifactsManagerOpts{LegacyRootDir: legacyRootDir, RootDir: rootDir}
	am, err := NewUserArtifactsManagerImpl(opts)
	if err != nil {
		t.Fatal(err)
	}
	chunk := UserArtifactChunk{
		Name:        "xyzz",
		OffsetBytes: 0,
		File:        strings.NewReader("lorem ipsum"),
	}

	err = am.UpdateArtifactWithDir("bar", chunk)

	if err == nil {
		t.Error("expected error")
	}
}

func TestUpdateArtifactWithDirSucceeds(t *testing.T) {
	wg := sync.WaitGroup{}
	legacyRootDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, legacyRootDir)
	rootDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, rootDir)
	opts := UserArtifactsManagerOpts{LegacyRootDir: legacyRootDir, RootDir: rootDir}
	am, err := NewUserArtifactsManagerImpl(opts)
	if err != nil {
		t.Fatal(err)
	}
	upDir, err := am.NewDir()
	if err != nil {
		t.Fatal(err)
	}
	chunk1 := UserArtifactChunk{
		Name:        "xyzz",
		File:        strings.NewReader("lore"),
		OffsetBytes: 0,
	}
	chunk2 := UserArtifactChunk{
		Name:        "xyzz",
		File:        strings.NewReader("m ip"),
		OffsetBytes: 0 + 4,
	}
	chunk3 := UserArtifactChunk{
		Name:        "xyzz",
		File:        strings.NewReader("sum"),
		OffsetBytes: 0 + 4 + 4,
	}
	chunks := [3]UserArtifactChunk{chunk1, chunk2, chunk3}
	wg.Add(3)

	for i := 0; i < len(chunks); i++ {
		go func(i int) {
			defer wg.Done()
			am.UpdateArtifactWithDir(upDir.Name, chunks[i])
		}(i)

	}

	wg.Wait()
	b, _ := ioutil.ReadFile(am.GetFilePath(upDir.Name, "xyzz"))
	if diff := cmp.Diff("lorem ipsum", string(b)); diff != "" {
		t.Errorf("aritfact content mismatch (-want +got):\n%s", diff)
	}
}

func getSha256Sum(data string) string {
	return fmt.Sprintf("%x", sha256.Sum256([]byte(data)))
}

func TestUpdateArtifactWithSingleChunkSucceeds(t *testing.T) {
	legacyRootDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, legacyRootDir)
	rootDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, rootDir)
	opts := UserArtifactsManagerOpts{LegacyRootDir: legacyRootDir, RootDir: rootDir}
	uam, err := NewUserArtifactsManagerImpl(opts)
	if err != nil {
		t.Fatal(err)
	}

	checksum := getSha256Sum(testFileData)
	chunk := UserArtifactChunk{
		Name:          testFileName,
		OffsetBytes:   0,
		SizeBytes:     int64(len(testFileData)),
		FileSizeBytes: int64(len(testFileData)),
		File:          strings.NewReader(testFileData),
	}
	if err := uam.UpdateArtifact(checksum, chunk); err != nil {
		t.Fatal(err)
	}
	b, err := ioutil.ReadFile(filepath.Join(uam.GetDirPath(checksum, false), testFileName))
	if err != nil {
		t.Fatal(err)
	}
	if diff := cmp.Diff(testFileData, string(b)); diff != "" {
		t.Fatalf("artifact content mismatch (-want +got):\n%s", diff)
	}
}

func TestUpdateArtifactFailsWithInvalidInput(t *testing.T) {
	checksum := getSha256Sum(testFileData)
	chunks := map[string]UserArtifactChunk{
		"NegativeChunkOffset": {
			Name:          testFileName,
			OffsetBytes:   -1234,
			SizeBytes:     int64(len(testFileData)),
			FileSizeBytes: int64(len(testFileData)),
			File:          strings.NewReader(testFileData),
		},
		"NegativeChunkSize": {
			Name:          testFileName,
			OffsetBytes:   0,
			SizeBytes:     -1234,
			FileSizeBytes: int64(len(testFileData)),
			File:          strings.NewReader(testFileData),
		},
		"NegativeFileSize": {
			Name:          testFileName,
			OffsetBytes:   0,
			SizeBytes:     int64(len(testFileData)),
			FileSizeBytes: -1234,
			File:          strings.NewReader(testFileData),
		},
		"ChunkSizeOverflow": {
			Name:          testFileName,
			OffsetBytes:   0,
			SizeBytes:     int64(len(testFileData)) + 1234,
			FileSizeBytes: int64(len(testFileData)),
			File:          strings.NewReader(testFileData),
		},
	}
	for name, chunk := range chunks {
		t.Run(name, func(t *testing.T) {
			legacyRootDir := orchtesting.TempDir(t)
			defer orchtesting.RemoveDir(t, legacyRootDir)
			rootDir := orchtesting.TempDir(t)
			defer orchtesting.RemoveDir(t, rootDir)
			opts := UserArtifactsManagerOpts{LegacyRootDir: legacyRootDir, RootDir: rootDir}
			uam, err := NewUserArtifactsManagerImpl(opts)
			if err != nil {
				t.Fatal(err)
			}

			if err := uam.UpdateArtifact(checksum, chunk); err == nil {
				t.Fatal("Expected an error")
			}
		})
	}
}

func TestUpdateArtifactAfterArtifactIsFullyUploadedFails(t *testing.T) {
	legacyRootDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, legacyRootDir)
	rootDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, rootDir)
	opts := UserArtifactsManagerOpts{LegacyRootDir: legacyRootDir, RootDir: rootDir}
	uam, err := NewUserArtifactsManagerImpl(opts)
	if err != nil {
		t.Fatal(err)
	}

	checksum := getSha256Sum(testFileData)
	chunk := UserArtifactChunk{
		Name:          testFileName,
		OffsetBytes:   0,
		SizeBytes:     int64(len(testFileData)),
		FileSizeBytes: int64(len(testFileData)),
		File:          strings.NewReader(testFileData),
	}
	if err := uam.UpdateArtifact(checksum, chunk); err != nil {
		t.Fatal(err)
	}
	if err := uam.UpdateArtifact(checksum, chunk); err == nil {
		t.Fatal("Expected an error")
	}
}

func constructSeparatedChunks(name, data string) []UserArtifactChunk {
	chunks := []UserArtifactChunk{}
	for idxStart := 0; idxStart < len(data); {
		idxEnd := idxStart*2 + 1
		if idxEnd > len(data) {
			idxEnd = len(data)
		}
		chunks = append(chunks, UserArtifactChunk{
			Name:          name,
			OffsetBytes:   int64(idxStart),
			SizeBytes:     int64(idxEnd - idxStart),
			FileSizeBytes: int64(len(data)),
			File:          strings.NewReader(data[idxStart:idxEnd]),
		})
		idxStart = idxEnd
	}
	return chunks
}

func TestUpdateArtifactWithMultipleSerialChunkSucceeds(t *testing.T) {
	legacyRootDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, legacyRootDir)
	rootDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, rootDir)
	opts := UserArtifactsManagerOpts{LegacyRootDir: legacyRootDir, RootDir: rootDir}
	uam, err := NewUserArtifactsManagerImpl(opts)
	if err != nil {
		t.Fatal(err)
	}

	checksum := getSha256Sum(testFileData)
	chunks := constructSeparatedChunks(testFileName, testFileData)
	for _, chunk := range chunks {
		if err := uam.UpdateArtifact(checksum, chunk); err != nil {
			t.Fatal(err)
		}
	}
	b, err := ioutil.ReadFile(filepath.Join(uam.GetDirPath(getSha256Sum(testFileData), false), testFileName))
	if err != nil {
		t.Fatal(err)
	}
	if diff := cmp.Diff(testFileData, string(b)); diff != "" {
		t.Fatalf("artifact content mismatch (-want +got):\n%s", diff)
	}
}

func TestUpdateArtifactWithMultipleParallelChunkSucceeds(t *testing.T) {
	legacyRootDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, legacyRootDir)
	rootDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, rootDir)
	opts := UserArtifactsManagerOpts{LegacyRootDir: legacyRootDir, RootDir: rootDir}
	uam, err := NewUserArtifactsManagerImpl(opts)
	if err != nil {
		t.Fatal(err)
	}

	checksum := getSha256Sum(testFileData)
	chunks := constructSeparatedChunks(testFileName, testFileData)
	wg := sync.WaitGroup{}
	wg.Add(len(chunks))
	for _, chunk := range chunks {
		go func(chunk UserArtifactChunk) {
			defer wg.Done()
			if err := uam.UpdateArtifact(checksum, chunk); err != nil {
				t.Error(err)
			}
		}(chunk)
	}
	wg.Wait()
	b, err := ioutil.ReadFile(filepath.Join(uam.GetDirPath(getSha256Sum(testFileData), false), testFileName))
	if err != nil {
		t.Fatal(err)
	}
	if diff := cmp.Diff(testFileData, string(b)); diff != "" {
		t.Fatalf("artifact content mismatch (-want +got):\n%s", diff)
	}
}
