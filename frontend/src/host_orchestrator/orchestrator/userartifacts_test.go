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
	"errors"
	"io/ioutil"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"testing"

	orchtesting "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/testing"
	apiv1 "github.com/google/android-cuttlefish/frontend/src/liboperator/api/v1"

	"github.com/google/go-cmp/cmp"
)

func TestNewDir(t *testing.T) {
	root := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, root)
	opts := UserArtifactsManagerOpts{RootDir: root}
	am := NewUserArtifactsManagerImpl(opts)

	upDir, err := am.NewDir()

	if err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(filepath.Join(root, upDir.Name)); errors.Is(err, os.ErrNotExist) {
		t.Errorf("upload dir %q does not exist", upDir.Name)
	}
}

func TestListDirsAndNoDirHasBeenCreated(t *testing.T) {
	root := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, root)
	opts := UserArtifactsManagerOpts{RootDir: root}
	am := NewUserArtifactsManagerImpl(opts)

	res, _ := am.ListDirs()

	exp := &apiv1.ListUploadDirectoriesResponse{Items: make([]*apiv1.UploadDirectory, 0)}
	if diff := cmp.Diff(exp, res); diff != "" {
		t.Errorf("response mismatch (-want +got):\n%s", diff)
	}
}

func TestListTokens(t *testing.T) {
	root := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, root)
	opts := UserArtifactsManagerOpts{RootDir: root}
	am := NewUserArtifactsManagerImpl(opts)
	am.NewDir()
	am.NewDir()

	res, err := am.ListDirs()

	if err != nil {
		t.Fatal(err)
	}
	entries, err := ioutil.ReadDir(root)
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

func TestCreateArtifactDirectoryDoesNotExist(t *testing.T) {
	root := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, root)
	opts := UserArtifactsManagerOpts{RootDir: root}
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
	root := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, root)
	opts := UserArtifactsManagerOpts{RootDir: root}
	am := NewUserArtifactsManagerImpl(opts)
	upDir, err := am.NewDir()
	if err != nil {
		t.Fatal(err)
	}
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
			am.UpdateArtifact(upDir.Name, chunks[i])
		}(i)

	}

	wg.Wait()
	b, _ := ioutil.ReadFile(am.GetFilePath(upDir.Name, "xyzz"))
	if diff := cmp.Diff("lorem ipsum", string(b)); diff != "" {
		t.Errorf("aritfact content mismatch (-want +got):\n%s", diff)
	}
}
