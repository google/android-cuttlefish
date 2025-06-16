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
	"bytes"
	"compress/gzip"
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

	"github.com/google/btree"
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

func getSha256Sum(data []byte) string {
	return fmt.Sprintf("%x", sha256.Sum256(data))
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

	checksum := getSha256Sum([]byte(testFileData))
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
	b, err := ioutil.ReadFile(filepath.Join(rootDir, checksum, testFileName))
	if err != nil {
		t.Fatal(err)
	}
	if diff := cmp.Diff(testFileData, string(b)); diff != "" {
		t.Fatalf("artifact content mismatch (-want +got):\n%s", diff)
	}
}

func TestUpdateArtifactFailsWithInvalidInput(t *testing.T) {
	checksum := getSha256Sum([]byte(testFileData))
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

	checksum := getSha256Sum([]byte(testFileData))
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

	checksum := getSha256Sum([]byte(testFileData))
	chunks := constructSeparatedChunks(testFileName, testFileData)
	for _, chunk := range chunks {
		if err := uam.UpdateArtifact(checksum, chunk); err != nil {
			t.Fatal(err)
		}
	}
	b, err := ioutil.ReadFile(filepath.Join(rootDir, checksum, testFileName))
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

	checksum := getSha256Sum([]byte(testFileData))
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
	b, err := ioutil.ReadFile(filepath.Join(rootDir, checksum, testFileName))
	if err != nil {
		t.Fatal(err)
	}
	if diff := cmp.Diff(testFileData, string(b)); diff != "" {
		t.Fatalf("artifact content mismatch (-want +got):\n%s", diff)
	}
}

func TestStatArtifactFailsArtifactNotFound(t *testing.T) {
	legacyRootDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, legacyRootDir)
	rootDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, rootDir)
	opts := UserArtifactsManagerOpts{LegacyRootDir: legacyRootDir, RootDir: rootDir}
	uam, err := NewUserArtifactsManagerImpl(opts)
	if err != nil {
		t.Fatal(err)
	}

	if _, err := uam.StatArtifact("foo"); err == nil {
		t.Fatal("Expected an error")
	}
}

func TestStatArtifactSucceeds(t *testing.T) {
	legacyRootDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, legacyRootDir)
	rootDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, rootDir)
	opts := UserArtifactsManagerOpts{LegacyRootDir: legacyRootDir, RootDir: rootDir}
	uam, err := NewUserArtifactsManagerImpl(opts)
	if err != nil {
		t.Fatal(err)
	}
	checksum := getSha256Sum([]byte(testFileData))
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

	if _, err := uam.StatArtifact(checksum); err != nil {
		t.Fatal(err)
	}
}

func getContents(dir string) (map[string]string, error) {
	contents := make(map[string]string)
	walkFunc := func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		if info.IsDir() {
			return nil
		}
		if relPath, err := filepath.Rel(dir, path); err != nil {
			return err
		} else if content, err := ioutil.ReadFile(path); err != nil {
			return err
		} else {
			contents[relPath] = string(content)
		}
		return nil
	}
	if err := filepath.Walk(dir, walkFunc); err != nil {
		return nil, err
	}
	return contents, nil
}

func createZip(dir string, contents map[string]string) (string, error) {
	zipFile, err := ioutil.TempFile(dir, "*.zip")
	if err != nil {
		return "", err
	}
	defer zipFile.Close()

	zipWriter := zip.NewWriter(zipFile)
	defer zipWriter.Close()

	for name, content := range contents {
		header := zip.FileHeader{
			Name: name,
		}
		writer, err := zipWriter.CreateHeader(&header)
		if err != nil {
			return "", err
		}
		if n, err := writer.Write([]byte(content)); err != nil {
			return "", err
		} else if n != len(content) {
			return "", fmt.Errorf("Failed to write entire file: %s", name)
		}
	}

	return zipFile.Name(), nil
}

func TestExtractArtifactSucceedsWithZipFormat(t *testing.T) {
	legacyRootDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, legacyRootDir)
	rootDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, rootDir)
	opts := UserArtifactsManagerOpts{LegacyRootDir: legacyRootDir, RootDir: rootDir}
	uam, err := NewUserArtifactsManagerImpl(opts)
	if err != nil {
		t.Fatal(err)
	}
	zipContents := map[string]string{
		"alpha.txt":   "This is alpha.\n",
		"bravo.txt":   "This is bravo.\n",
		"charlie.txt": "This is charlie.\n",
		"delta.txt":   "This is delta.\n",
	}
	tempDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, tempDir)
	zipFile, err := createZip(tempDir, zipContents)
	if err != nil {
		t.Fatal(err)
	}
	data, err := ioutil.ReadFile(zipFile)
	if err != nil {
		t.Fatal(err)
	}
	checksum := getSha256Sum(data)
	chunk := UserArtifactChunk{
		Name:          filepath.Base(zipFile),
		OffsetBytes:   0,
		SizeBytes:     int64(len(data)),
		FileSizeBytes: int64(len(data)),
		File:          bytes.NewReader(data),
	}
	if err := uam.UpdateArtifact(checksum, chunk); err != nil {
		t.Fatal(err)
	}

	if err := uam.ExtractArtifact(checksum); err != nil {
		t.Fatal(err)
	}
	if got, err := getContents(filepath.Join(rootDir, fmt.Sprintf("%s_extracted", checksum))); err != nil {
		t.Fatal(err)
	} else if diff := cmp.Diff(zipContents, got); diff != "" {
		t.Fatalf("chunk state mismatch (-want +got):\n%s", diff)
	}
}

func getSubdirs(path string) []string {
	// Remove leading and trailing slashes
	path = strings.Trim(path, "/")
	if len(path) == 0 {
		return []string{}
	}
	subdirs := []string{}
	subdir := ""
	for _, name := range strings.Split(path, "/") {
		subdir += name + "/"
		subdirs = append(subdirs, subdir)
	}
	return subdirs
}

func createTarGz(dir string, contents map[string]string) (string, error) {
	tarFile, err := ioutil.TempFile(dir, "*.tar.gz")
	if err != nil {
		return "", err
	}
	defer tarFile.Close()

	gzipWriter := gzip.NewWriter(tarFile)
	defer gzipWriter.Close()

	tarWriter := tar.NewWriter(gzipWriter)
	defer tarWriter.Close()

	directories := map[string]struct{}{}

	for name, content := range contents {
		dir, _ := filepath.Split(name)
		dirPaths := getSubdirs(dir)
		for _, dp := range dirPaths {
			if _, alreadyAdded := directories[dp]; alreadyAdded {
				continue
			}
			directories[dp] = struct{}{}
			header := tar.Header{
				Name:     dp,
				Mode:     0555,
				Typeflag: tar.TypeDir,
			}
			if err := tarWriter.WriteHeader(&header); err != nil {
				return "", err
			}
		}
		header := tar.Header{
			Name: name,
			Size: int64(len(content)),
			Mode: 0555,
		}
		if err := tarWriter.WriteHeader(&header); err != nil {
			return "", err
		}
		if n, err := tarWriter.Write([]byte(content)); err != nil {
			return "", err
		} else if n != len(content) {
			return "", fmt.Errorf("Failed to write entire file: %s", name)
		}
	}
	return tarFile.Name(), nil
}

func TestExtractArtifactSucceedsWithTarGzFormat(t *testing.T) {
	legacyRootDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, legacyRootDir)
	rootDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, rootDir)
	opts := UserArtifactsManagerOpts{LegacyRootDir: legacyRootDir, RootDir: rootDir}
	uam, err := NewUserArtifactsManagerImpl(opts)
	if err != nil {
		t.Fatal(err)
	}
	tarContents := map[string]string{
		"foo/alpha.txt":   "This is alpha.\n",
		"foo/bravo.txt":   "This is bravo.\n",
		"bar/charlie.txt": "This is charlie.\n",
		"delta.txt":       "This is delta.\n",
	}
	tempDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, tempDir)
	tarFile, err := createTarGz(tempDir, tarContents)
	if err != nil {
		t.Fatal(err)
	}
	data, err := ioutil.ReadFile(tarFile)
	if err != nil {
		t.Fatal(err)
	}
	checksum := getSha256Sum(data)
	chunk := UserArtifactChunk{
		Name:          filepath.Base(tarFile),
		OffsetBytes:   0,
		SizeBytes:     int64(len(data)),
		FileSizeBytes: int64(len(data)),
		File:          bytes.NewReader(data),
	}
	if err := uam.UpdateArtifact(checksum, chunk); err != nil {
		t.Fatal(err)
	}

	if err := uam.ExtractArtifact(checksum); err != nil {
		t.Fatal(err)
	}
	if got, err := getContents(filepath.Join(rootDir, fmt.Sprintf("%s_extracted", checksum))); err != nil {
		t.Fatal(err)
	} else if diff := cmp.Diff(tarContents, got); diff != "" {
		t.Fatalf("chunk state mismatch (-want +got):\n%s", diff)
	}
}

func TestExtractArtifactFailsWithInvalidFileFormat(t *testing.T) {
	legacyRootDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, legacyRootDir)
	rootDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, rootDir)
	opts := UserArtifactsManagerOpts{LegacyRootDir: legacyRootDir, RootDir: rootDir}
	uam, err := NewUserArtifactsManagerImpl(opts)
	if err != nil {
		t.Fatal(err)
	}
	checksum := getSha256Sum([]byte(testFileData))
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

	if err := uam.ExtractArtifact(checksum); err == nil {
		t.Fatal("Expected an error")
	}
}

func TestExtractArtifactAfterArtifactIsFullyExtractedFails(t *testing.T) {
	legacyRootDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, legacyRootDir)
	rootDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, rootDir)
	opts := UserArtifactsManagerOpts{LegacyRootDir: legacyRootDir, RootDir: rootDir}
	uam, err := NewUserArtifactsManagerImpl(opts)
	if err != nil {
		t.Fatal(err)
	}
	tempDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, tempDir)
	archive, err := createTarGz(tempDir, map[string]string{"file": "content\n"})
	if err != nil {
		t.Fatal(err)
	}
	data, err := ioutil.ReadFile(archive)
	if err != nil {
		t.Fatal(err)
	}
	checksum := getSha256Sum(data)
	chunk := UserArtifactChunk{
		Name:          filepath.Base(archive),
		OffsetBytes:   0,
		SizeBytes:     int64(len(data)),
		FileSizeBytes: int64(len(data)),
		File:          bytes.NewReader(data),
	}
	if err := uam.UpdateArtifact(checksum, chunk); err != nil {
		t.Fatal(err)
	}
	if err := uam.ExtractArtifact(checksum); err != nil {
		t.Fatal(err)
	}

	if err := uam.ExtractArtifact(checksum); err == nil {
		t.Fatal("Expected an error")
	}
}

func TestExtractArtifactFailsArtifactNotFound(t *testing.T) {
	legacyRootDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, legacyRootDir)
	rootDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, rootDir)
	opts := UserArtifactsManagerOpts{LegacyRootDir: legacyRootDir, RootDir: rootDir}
	uam, err := NewUserArtifactsManagerImpl(opts)
	if err != nil {
		t.Fatal(err)
	}

	if err := uam.ExtractArtifact("foo"); err == nil {
		t.Fatal("Expected an error")
	}
}

func getChunkStateItemList(cs *chunkState) []chunkStateItem {
	items := []chunkStateItem{}
	cs.items.Ascend(func(item btree.Item) bool {
		items = append(items, item.(chunkStateItem))
		return true
	})
	return items
}

func TestChunkStateUpdateSucceedsForSeparatedChunks(t *testing.T) {
	cs := NewChunkState(100)
	cs.Update(0, 10)
	cs.Update(40, 50)
	cs.Update(90, 100)
	items := getChunkStateItemList(cs)
	expected := []chunkStateItem{
		{offset: 0, isUpdated: true},
		{offset: 10, isUpdated: false},
		{offset: 40, isUpdated: true},
		{offset: 50, isUpdated: false},
		{offset: 90, isUpdated: true},
		{offset: 100, isUpdated: false},
	}
	if diff := cmp.Diff(expected, items, cmp.AllowUnexported(chunkStateItem{})); diff != "" {
		t.Fatalf("chunk state mismatch (-want +got):\n%s", diff)
	}
}

func TestChunkStateUpdateSucceedsForSubranges(t *testing.T) {
	cs := NewChunkState(100)
	cs.Update(40, 50)
	cs.Update(60, 70)
	cs.Update(30, 80)
	cs.Update(50, 60)
	cs.Update(65, 75)
	items := getChunkStateItemList(cs)
	expected := []chunkStateItem{
		{offset: 30, isUpdated: true},
		{offset: 80, isUpdated: false},
	}
	if diff := cmp.Diff(expected, items, cmp.AllowUnexported(chunkStateItem{})); diff != "" {
		t.Fatalf("chunk state mismatch (-want +got):\n%s", diff)
	}
}

func TestChunkStateUpdateSucceedsForOverlappingRanges(t *testing.T) {
	cs := NewChunkState(100)
	cs.Update(10, 30)
	cs.Update(20, 40)
	cs.Update(70, 90)
	cs.Update(60, 80)
	items := getChunkStateItemList(cs)
	expected := []chunkStateItem{
		{offset: 10, isUpdated: true},
		{offset: 40, isUpdated: false},
		{offset: 60, isUpdated: true},
		{offset: 90, isUpdated: false},
	}
	if diff := cmp.Diff(expected, items, cmp.AllowUnexported(chunkStateItem{})); diff != "" {
		t.Fatalf("chunk state mismatch (-want +got):\n%s", diff)
	}
}

func TestChunkStateUpdateSucceedsForSameStartOrEnd(t *testing.T) {
	cs := NewChunkState(100)
	cs.Update(10, 15)
	cs.Update(10, 12)
	cs.Update(20, 22)
	cs.Update(20, 25)
	cs.Update(55, 60)
	cs.Update(58, 60)
	cs.Update(65, 70)
	cs.Update(68, 70)
	items := getChunkStateItemList(cs)
	expected := []chunkStateItem{
		{offset: 10, isUpdated: true},
		{offset: 15, isUpdated: false},
		{offset: 20, isUpdated: true},
		{offset: 25, isUpdated: false},
		{offset: 55, isUpdated: true},
		{offset: 60, isUpdated: false},
		{offset: 65, isUpdated: true},
		{offset: 70, isUpdated: false},
	}
	if diff := cmp.Diff(expected, items, cmp.AllowUnexported(chunkStateItem{})); diff != "" {
		t.Fatalf("chunk state mismatch (-want +got):\n%s", diff)
	}
}

func TestChunkStateUpdateSucceedsForMergingChunks(t *testing.T) {
	cs := NewChunkState(100)
	cs.Update(10, 20)
	cs.Update(30, 40)
	cs.Update(50, 60)
	cs.Update(20, 30)
	cs.Update(40, 50)
	items := getChunkStateItemList(cs)
	expected := []chunkStateItem{
		{offset: 10, isUpdated: true},
		{offset: 60, isUpdated: false},
	}
	if diff := cmp.Diff(expected, items, cmp.AllowUnexported(chunkStateItem{})); diff != "" {
		t.Fatalf("chunk state mismatch (-want +got):\n%s", diff)
	}
}

func TestChunkStateUpdateFailsForInvalidRanges(t *testing.T) {
	cs := NewChunkState(100)
	if err := cs.Update(-1, 10); err == nil {
		t.Fatalf("expected an error")
	}
	if err := cs.Update(90, 101); err == nil {
		t.Fatalf("expected an error")
	}
	if err := cs.Update(50, 50); err == nil {
		t.Fatalf("expected an error")
	}
	if err := cs.Update(80, 40); err == nil {
		t.Fatalf("expected an error")
	}
	items := getChunkStateItemList(cs)
	expected := []chunkStateItem{}
	if diff := cmp.Diff(expected, items, cmp.AllowUnexported(chunkStateItem{})); diff != "" {
		t.Fatalf("chunk state mismatch (-want +got):\n%s", diff)
	}
}

func TestChunkStateIsCompleted(t *testing.T) {
	cs := NewChunkState(100)
	cs.Update(0, 20)
	cs.Update(80, 100)
	cs.Update(20, 80)
	if !cs.IsCompleted() {
		t.Fatalf("expected as completed")
	}
}

func TestChunkStateIsNotCompletedWithMissingStart(t *testing.T) {
	cs := NewChunkState(100)
	cs.Update(1, 100)
	if cs.IsCompleted() {
		t.Fatalf("expected as not completed")
	}
}

func TestChunkStateIsNotCompletedWithMissingIntermediate(t *testing.T) {
	cs := NewChunkState(100)
	cs.Update(0, 20)
	cs.Update(80, 100)
	cs.Update(20, 79)
	if cs.IsCompleted() {
		t.Fatalf("expected as not completed")
	}
}

func TestChunkStateIsNotCompletedWithMissingEnd(t *testing.T) {
	cs := NewChunkState(100)
	cs.Update(0, 99)
	if cs.IsCompleted() {
		t.Fatalf("expected as not completed")
	}
}
