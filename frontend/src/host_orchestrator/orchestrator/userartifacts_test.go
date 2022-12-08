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
	"strings"
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

	err := am.CreateUpdateArtifact("bar", "xyzz", strings.NewReader("lorem ispum"))

	if err == nil {
		t.Error("expected error")
	}
}

func TestCreateArtifactSucceeds(t *testing.T) {
	dir := tempDir(t)
	defer removeDir(t, dir)
	opts := UserArtifactsManagerOpts{
		RootDir:     dir,
		NameFactory: func() string { return "foo" },
	}
	am := NewUserArtifactsManagerImpl(opts)
	am.NewDir()

	err := am.CreateUpdateArtifact("foo", "xyzz", strings.NewReader("lorem ipsum"))

	if err != nil {
		t.Fatal(err)
	}
	b, _ := ioutil.ReadFile(am.GetFullPath("foo", "xyzz"))
	if diff := cmp.Diff("lorem ipsum", string(b)); diff != "" {
		t.Errorf("aritfact content mismatch (-want +got):\n%s", diff)
	}
}

func TestUpdateArtifactSucceds(t *testing.T) {
	dir := tempDir(t)
	defer removeDir(t, dir)
	opts := UserArtifactsManagerOpts{
		RootDir:     dir,
		NameFactory: func() string { return "foo" },
	}
	am := NewUserArtifactsManagerImpl(opts)
	am.NewDir()
	am.CreateUpdateArtifact("foo", "xyzz", strings.NewReader("lorem ipsum"))

	err := am.CreateUpdateArtifact("foo", "xyzz", strings.NewReader("dolor sit amet"))

	if err != nil {
		t.Fatal(err)
	}
	b, _ := ioutil.ReadFile(am.GetFullPath("foo", "xyzz"))
	if diff := cmp.Diff("dolor sit amet", string(b)); diff != "" {
		t.Errorf("aritfact content mismatch (-want +got):\n%s", diff)
	}
}
