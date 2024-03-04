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
	"context"
	"fmt"
	"io"
	"os/exec"
	"path"
	"testing"
	"time"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/liboperator/api/v1"

	"github.com/google/go-cmp/cmp"
)

type AlwaysSucceedsValidator struct{}

func (AlwaysSucceedsValidator) Validate() error {
	return nil
}

const fakeLatesGreenBuildID = "9551522"

const fakeCVDUser = "fakecvduser"

type fakeBuildAPI struct{}

func (fakeBuildAPI) GetLatestGreenBuildID(string, string) (string, error) {
	return fakeLatesGreenBuildID, nil
}

func (fakeBuildAPI) DownloadArtifact(string, string, string, io.Writer) error {
	return nil
}

const fakeProductName = "aosp_foo_x86_64"

func (fakeBuildAPI) ProductName(buildID, target string) (string, error) {
	return fakeProductName, nil
}

const fakeUUID = "123e4567-"

var fakeUUIDGen = func() string { return fakeUUID }

func TestSliceItoa(t *testing.T) {
	tests := []struct {
		in  []uint32
		out []string
	}{
		{
			in:  []uint32{},
			out: []string{},
		},
		{
			in:  []uint32{79, 83, 89, 97},
			out: []string{"79", "83", "89", "97"},
		},
	}
	for _, tc := range tests {

		res := SliceItoa(tc.in)

		if diff := cmp.Diff(tc.out, res); diff != "" {
			t.Errorf("result mismatch (-want +got):\n%s", diff)
		}
	}

}

func execCtxAlwaysSucceeds(ctx context.Context, name string, args ...string) *exec.Cmd {
	return exec.Command("true")
}

func isCvdSubCommand(name string, args ...string) bool {
	// All cvd executions are run through `sudo`.
	if name != "sudo" {
		return false
	}
	// cvd alone not a cvd subcommand
	if path.Base(args[len(args)-1]) == "cvd" {
		return false
	}
	return true
}

func execCtxCvdSubcmdFails(ctx context.Context, name string, args ...string) *exec.Cmd {
	if isCvdSubCommand(name, args...) {
		return exec.Command("false")
	}
	return exec.Command("true")
}

const testFakeBinaryDelayMs = 100 * time.Millisecond

func execCtxCvdSubcmdDelays(ctx context.Context, name string, args ...string) *exec.Cmd {
	if isCvdSubCommand(name, args...) {
		return exec.Command(fmt.Sprintf("sleep %f", float64(testFakeBinaryDelayMs)/1000_000_000))
	}
	return exec.Command("true")
}

func containsStr(values []string, t string) bool {
	for _, v := range values {
		if v == t {
			return true
		}
	}
	return false
}

func androidCISource(buildID, target string) *apiv1.BuildSource {
	return &apiv1.BuildSource{
		AndroidCIBuildSource: &apiv1.AndroidCIBuildSource{
			MainBuild: &apiv1.AndroidCIBuild{
				BuildID: buildID,
				Target:  target,
			},
		},
	}
}
