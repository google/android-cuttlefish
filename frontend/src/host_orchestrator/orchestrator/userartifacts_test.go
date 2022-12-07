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
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestNewDir(t *testing.T) {
	dir := tempDir(t)
	defer removeDir(t, dir)
	opts := UserArtifactsManagerOpts{
		RootDir:      dir,
		NamesFactory: func() string { return "foo" },
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
		RootDir:      dir,
		NamesFactory: func() string { return testUUID },
	}
	am := NewUserArtifactsManagerImpl(opts)
	am.NewDir()

	_, err := am.NewDir()

	if err == nil {
		t.Error("expected error")
	}
}
