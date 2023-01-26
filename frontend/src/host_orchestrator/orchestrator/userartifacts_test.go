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
	"fmt"
	"io/ioutil"
	"path/filepath"
	"runtime"
	"strings"
	"sync"
	"testing"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/liboperator/api/v1"

	"github.com/google/go-cmp/cmp"
)

func TestNewDir(t *testing.T) {
	dir := tempDir(t)
	defer removeDir(t, dir)
	opts := UserArtifactsManagerOpts{
		RootDir:     dir,
		NameFactory: func() string { return "foo" },
	}
	am := NewUserArtifactsManagerImpl(opts)

	upDir, _ := am.NewDir()

	if diff := cmp.Diff("foo", upDir.Name); diff != "" {
		t.Errorf("name mismatch (-want +got):\n%s", diff)
	}
}

func TestNewDirAndDirNameAlreadyExists(t *testing.T) {
	dir := tempDir(t)
	defer removeDir(t, dir)
	testUUID := "foo"
	opts := UserArtifactsManagerOpts{
		RootDir:     dir,
		NameFactory: func() string { return testUUID },
	}
	am := NewUserArtifactsManagerImpl(opts)
	am.NewDir()

	_, err := am.NewDir()

	if err == nil {
		t.Error("expected error")
	}
}

func TestListDirsAndNoDirHasBeenCreated(t *testing.T) {
	dir := tempDir(t)
	defer removeDir(t, dir)
	opts := UserArtifactsManagerOpts{
		RootDir:     dir,
		NameFactory: func() string { return "foo" },
	}
	am := NewUserArtifactsManagerImpl(opts)

	res, _ := am.ListDirs()

	exp := &apiv1.ListUploadDirectoriesResponse{Items: make([]*apiv1.UploadDirectory, 0)}
	if diff := cmp.Diff(exp, res); diff != "" {
		t.Errorf("response mismatch (-want +got):\n%s", diff)
	}
}

func TestListTokens(t *testing.T) {
	dir := tempDir(t)
	defer removeDir(t, dir)
	namesCounter := 0
	opts := UserArtifactsManagerOpts{
		RootDir: dir,
		NameFactory: func() string {
			namesCounter++
			return fmt.Sprintf("foo-%d", namesCounter)
		},
	}
	am := NewUserArtifactsManagerImpl(opts)
	am.NewDir()
	am.NewDir()

	res, _ := am.ListDirs()

	exp := &apiv1.ListUploadDirectoriesResponse{
		Items: []*apiv1.UploadDirectory{
			{Name: "foo-1"},
			{Name: "foo-2"},
		},
	}
	if diff := cmp.Diff(exp, res); diff != "" {
		t.Errorf("response mismatch (-want +got):\n%s", diff)
	}
}

func TestCreateArtifactDirectoryDoesNotExist(t *testing.T) {
	dir := tempDir(t)
	defer removeDir(t, dir)
	opts := UserArtifactsManagerOpts{
		RootDir:     dir,
		NameFactory: func() string { return "foo" },
	}
	am := NewUserArtifactsManagerImpl(opts)
	chunk := UserArtifactChunk{
		Name:        "xyzz",
		ChunkNumber: 1,
		ChunkTotal:  11,
		File:        strings.NewReader("lorem ipsum"),
	}

	err := am.UpdateArtifact("bar", chunk)

	if err == nil {
		t.Error("expected error")
	}
}

func TestCreateArtifactsSucceeds(t *testing.T) {
	wg := sync.WaitGroup{}
	dir := tempDir(t)
	defer removeDir(t, dir)
	opts := UserArtifactsManagerOpts{
		RootDir:     dir,
		NameFactory: func() string { return "foo" },
	}
	am := NewUserArtifactsManagerImpl(opts)
	am.NewDir()
	chunk1 := UserArtifactChunk{
		Name:           "xyzz",
		ChunkNumber:    1,
		ChunkTotal:     3,
		ChunkSizeBytes: 4,
		File:           strings.NewReader("lore"),
	}
	chunk2 := UserArtifactChunk{
		Name:           "xyzz",
		ChunkNumber:    2,
		ChunkTotal:     3,
		ChunkSizeBytes: 4,
		File:           strings.NewReader("m ip"),
	}
	chunk3 := UserArtifactChunk{
		Name:           "xyzz",
		ChunkNumber:    3,
		ChunkTotal:     3,
		ChunkSizeBytes: 4,
		File:           strings.NewReader("sum"),
	}
	chunks := [3]UserArtifactChunk{chunk1, chunk2, chunk3}
	wg.Add(3)

	for i := 0; i < len(chunks); i++ {
		go func(i int) {
			defer wg.Done()
			am.UpdateArtifact("foo", chunks[i])
		}(i)

	}

	wg.Wait()
	b, _ := ioutil.ReadFile(am.GetFilePath("foo", "xyzz"))
	if diff := cmp.Diff("lorem ipsum", string(b)); diff != "" {
		t.Errorf("aritfact content mismatch (-want +got):\n%s", diff)
	}
}

func TestUntar(t *testing.T) {
	dir := tempDir(t)
	defer removeDir(t, dir)

	Untar(dir, getTestTarFilename())

	b, _ := ioutil.ReadFile(dir + "/foo_dir/foo.txt")
	if diff := cmp.Diff("foo\n", string(b)); diff != "" {
		t.Errorf("aritfact content mismatch (-want +got):\n%s", diff)
	}
	b, _ = ioutil.ReadFile(dir + "/foo_dir/link_foo.txt")
	if diff := cmp.Diff("foo\n", string(b)); diff != "" {
		t.Errorf("symlink content mismatch (-want +got):\n%s", diff)
	}
}

func getTestTarFilename() string {
	_, filename, _, _ := runtime.Caller(0)
	dir := filepath.Dir(filename)
	return dir + "/testdata/foo.tar.gz"
}
