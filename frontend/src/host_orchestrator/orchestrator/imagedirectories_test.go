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
	"io/ioutil"
	"os"
	"path/filepath"
	"testing"

	orchtesting "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/testing"
	"github.com/google/go-cmp/cmp"
)

func TestCreateImageDirectory(t *testing.T) {
	rootDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, rootDir)
	opts := ImageDirectoriesManagerOpts{RootDir: rootDir}
	idm := NewImageDirectoriesManagerImpl(opts)

	dir, err := idm.CreateImageDirectory()
	if err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(filepath.Join(rootDir, dir)); err != nil {
		t.Fatal(err)
	}
}

func TestUpdateImageDirectorySucceeds(t *testing.T) {
	rootDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, rootDir)
	opts := ImageDirectoriesManagerOpts{RootDir: rootDir}
	idm := NewImageDirectoriesManagerImpl(opts)
	imageDir, err := idm.CreateImageDirectory()
	if err != nil {
		t.Fatal(err)
	}
	srcDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, srcDir)
	src := filepath.Join(srcDir, "foo.txt")
	f, err := os.Create(src)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	if err := ioutil.WriteFile(src, []byte("hello_world"), 0600); err != nil {
		t.Fatal(err)
	}

	if err := idm.UpdateImageDirectory(imageDir, srcDir); err != nil {
		t.Fatal(err)
	}
	if output, err := ioutil.ReadFile(filepath.Join(rootDir, imageDir, "foo.txt")); err != nil {
		t.Fatal(err)
	} else if diff := cmp.Diff("hello_world", string(output)); diff != "" {
		t.Errorf("response mismatch (-want +got):\n%s", diff)
	}
}

func TestUpdateImageDirectorySucceedsWithModification(t *testing.T) {
	rootDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, rootDir)
	opts := ImageDirectoriesManagerOpts{RootDir: rootDir}
	idm := NewImageDirectoriesManagerImpl(opts)
	imageDir, err := idm.CreateImageDirectory()
	if err != nil {
		t.Fatal(err)
	}
	srcDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, srcDir)
	src := filepath.Join(srcDir, "foo.txt")
	f, err := os.Create(src)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	if err := ioutil.WriteFile(src, []byte("hello_world"), 0600); err != nil {
		t.Fatal(err)
	}
	if err := idm.UpdateImageDirectory(imageDir, srcDir); err != nil {
		t.Fatal(err)
	}
	if err := ioutil.WriteFile(src, []byte("hello_world_again"), 0600); err != nil {
		t.Fatal(err)
	}

	if err := idm.UpdateImageDirectory(imageDir, srcDir); err != nil {
		t.Fatal(err)
	}
	if output, err := ioutil.ReadFile(filepath.Join(rootDir, imageDir, "foo.txt")); err != nil {
		t.Fatal(err)
	} else if diff := cmp.Diff("hello_world_again", string(output)); diff != "" {
		t.Errorf("response mismatch (-want +got):\n%s", diff)
	}
}

func TestUpdateImageDirectoryFailsWhenImageDirectoryDoesNotExist(t *testing.T) {
	rootDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, rootDir)
	opts := ImageDirectoriesManagerOpts{RootDir: rootDir}
	idm := NewImageDirectoriesManagerImpl(opts)
	srcDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, srcDir)

	if err := idm.UpdateImageDirectory("foo", srcDir); err == nil {
		t.Error("expected error")
	}
}

func TestUpdateImageDirectoryFailsWhenSrcDirectoryDoesNotExist(t *testing.T) {
	rootDir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, rootDir)
	opts := ImageDirectoriesManagerOpts{RootDir: rootDir}
	idm := NewImageDirectoriesManagerImpl(opts)
	imageDir, err := idm.CreateImageDirectory()
	if err != nil {
		t.Fatal(err)
	}

	if err := idm.UpdateImageDirectory(imageDir, "foo"); err == nil {
		t.Error("expected error")
	}
}
