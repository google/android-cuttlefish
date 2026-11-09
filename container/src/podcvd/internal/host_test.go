// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//	http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package internal

import (
	"context"
	"errors"
	"fmt"
	"io"
	"maps"
	"path/filepath"
	"reflect"
	"slices"
	"testing"
)

type hostManager struct {
	CuttlefishContainerManager
	containers  map[string]ContainerListEntry
	failCreate  bool
	execResults map[string]execResult
}

type execResult struct {
	wantCmd []string
	output  string
	fail    bool
}

func (h hostManager) ImageExists(_ context.Context, _ string) (bool, error) {
	return true, nil
}

func (h hostManager) ListContainers(_ context.Context, _ bool) ([]ContainerListEntry, error) {
	return slices.Collect(maps.Values(h.containers)), nil
}

func (h hostManager) ContainerExists(_ context.Context, name string) (bool, error) {
	_, ok := h.containers[name]
	return ok, nil
}

func (h hostManager) CreateAndStartContainer(
	_ context.Context, _ []string, _ string) (string, error) {
	if h.failCreate {
		return "", errors.New("failed to create container")
	}
	return "created_container_id", nil
}

func (h hostManager) ExecOnContainer(
	_ context.Context, ctr string, cmd []string, _ io.Reader, stdout, _ io.Writer) error {
	e := h.execResults[ctr]
	if e.fail {
		return errors.New("exec failed")
	}
	if e.wantCmd == nil {
		return nil
	}
	if !slices.Equal(e.wantCmd, cmd) {
		return errors.New("exec failed")
	}
	io.WriteString(stdout, e.output)
	return nil
}

func (h hostManager) StopAndRemoveContainer(_ context.Context, ctr string) error {
	if _, ok := h.containers[ctr]; !ok {
		return fmt.Errorf("no such container %q", ctr)
	}
	return nil
}

func TestDeleteCuttlefishHost(t *testing.T) {
	tests := []struct {
		name      string
		ccm       hostManager
		groupName string
		wantErr   bool
	}{
		{
			name: "success",
			ccm: hostManager{
				containers: map[string]ContainerListEntry{ContainerName("cvd_1"): {}},
			},
			groupName: "cvd_1",
		},
		{
			name: "failure",
			ccm: hostManager{
				containers: map[string]ContainerListEntry{ContainerName("cvd_1"): {}},
			},
			groupName: "cvd_2",
			wantErr:   true,
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			err := DeleteCuttlefishHost(tc.ccm, tc.groupName)
			if gotErr := err != nil; gotErr != tc.wantErr {
				t.Errorf("DeleteCuttlefishHost(%+v, %q) error = %v; want error %v",
					tc.ccm, tc.groupName, err, tc.wantErr)
			}
		})
	}
}

func TestCreateToolingHost(t *testing.T) {
	t.Setenv("XDG_DATA_HOME", t.TempDir())

	tests := []struct {
		name    string
		ccm     hostManager
		wantErr bool
	}{
		{
			name: "existing",
			ccm: hostManager{
				containers: map[string]ContainerListEntry{"tooling": {}},
				failCreate: true,
			},
		},
		{
			name: "new",
			ccm:  hostManager{},
		},
		{
			name: "failure",
			ccm: hostManager{
				containers: map[string]ContainerListEntry{ContainerName("cvd_1"): {}},
				failCreate: true,
			},
			wantErr: true,
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			err := CreateToolingHost(tc.ccm)
			if gotErr := err != nil; gotErr != tc.wantErr {
				t.Errorf("CreateToolingHost(%+v) error = %v; want error %v",
					tc.ccm, err, tc.wantErr)
			}
		})
	}
}

func TestDeleteToolingHost(t *testing.T) {
	tests := []struct {
		name    string
		ccm     hostManager
		wantErr bool
	}{
		{
			name: "success",
			ccm:  hostManager{containers: map[string]ContainerListEntry{"tooling": {}}},
		},
		{
			name: "failure",
			ccm: hostManager{
				containers: map[string]ContainerListEntry{ContainerName("cvd_1"): {}},
			},
			wantErr: true,
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			err := DeleteToolingHost(tc.ccm)
			if gotErr := err != nil; gotErr != tc.wantErr {
				t.Errorf("DeleteToolingHost(%+v) error = %v; want error %v",
					tc.ccm, err, tc.wantErr)
			}
		})
	}
}

func TestExecFetchCmdOnDisposableHost(t *testing.T) {
	t.Setenv("XDG_DATA_HOME", t.TempDir())
	targetDir := t.TempDir()

	tests := []struct {
		name    string
		ccm     hostManager
		args    []string
		wantErr bool
	}{
		{
			name: "success",
			ccm: hostManager{
				containers: map[string]ContainerListEntry{"created_container_id": {}},
				execResults: map[string]execResult{
					"created_container_id": {
						wantCmd: []string{
							"cvd",
							"fetch",
							"--default_build=aosp-main/aosp_cf_x86_64_phone-userdebug",
							"--target_directory=" + targetDir,
						},
					},
				},
			},
			args: []string{
				"fetch",
				"--default_build=aosp-main/aosp_cf_x86_64_phone-userdebug",
				"--target_directory=" + targetDir,
			},
		},
		{
			name: "missing target directory",
			ccm: hostManager{
				containers: map[string]ContainerListEntry{"created_container_id": {}},
			},
			args: []string{
				"fetch",
				"--default_build=aosp-main/aosp_cf_x86_64_phone-userdebug",
			},
			wantErr: true,
		},
		{
			name: "nonexistent target directory",
			ccm: hostManager{
				containers: map[string]ContainerListEntry{"created_container_id": {}},
			},
			args: []string{
				"fetch",
				"--default_build=aosp-main/aosp_cf_x86_64_phone-userdebug",
				"--target_directory=" + filepath.Join(targetDir, "missing"),
			},
			wantErr: true,
		},
		{
			name: "fetch failure",
			ccm: hostManager{
				containers:  map[string]ContainerListEntry{"created_container_id": {}},
				execResults: map[string]execResult{"created_container_id": {fail: true}},
			},
			args: []string{
				"fetch",
				"--default_build=aosp-main/aosp_cf_x86_64_phone-userdebug",
				"--target_directory=" + targetDir,
			},
			wantErr: true,
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			cvdArgs, err := ParseCvdArgs(tc.args)
			if err != nil {
				t.Fatalf("ParseCvdArgs(%q) failed: %v", tc.args, err)
			}
			err = ExecFetchCmdOnDisposableHost(tc.ccm, cvdArgs)
			if gotErr := err != nil; gotErr != tc.wantErr {
				t.Errorf("ExecFetchCmdOnDisposableHost(%+v, %q) error = %v; want error %v",
					tc.ccm, tc.args, err, tc.wantErr)
			}
		})
	}
}

func TestExecOnAllCuttlefishHosts(t *testing.T) {
	t.Setenv(envClientID, "")

	tests := []struct {
		name           string
		ccm            hostManager
		subcommandArgs []string
		want           []HostExecResult
		wantErr        bool
	}{
		{
			name: "fleet",
			ccm: hostManager{
				containers: map[string]ContainerListEntry{
					ContainerName("cvd_1"): {
						Labels: map[string]string{"created_by": "podcvd", "group_name": "cvd_1"},
						Ports:  []PortBinding{{IP: "192.168.80.1", PrivatePort: 1443}},
					},
					ContainerName("cvd_2"): {
						Labels: map[string]string{"created_by": "podcvd", "group_name": "cvd_2"},
						Ports:  []PortBinding{{IP: "192.168.80.2", PrivatePort: 1443}},
					},
				},
				execResults: map[string]execResult{
					ContainerName("cvd_1"): {
						wantCmd: []string{"cvd", "fleet"},
						output:  `{"groups":[{"group_name":"cvd_1"}]}`,
					},
					ContainerName("cvd_2"): {
						wantCmd: []string{"cvd", "fleet"},
						output:  `{"groups":[{"group_name":"cvd_2"}]}`,
					},
				},
			},
			subcommandArgs: []string{"cvd", "fleet"},
			want: []HostExecResult{
				{
					GroupName: "cvd_1",
					IP:        "192.168.80.1",
					Stdout:    []byte(`{"groups":[{"group_name":"cvd_1"}]}`),
				},
				{
					GroupName: "cvd_2",
					IP:        "192.168.80.2",
					Stdout:    []byte(`{"groups":[{"group_name":"cvd_2"}]}`),
				},
			},
		},
		{
			name: "ps",
			ccm: hostManager{
				containers: map[string]ContainerListEntry{
					ContainerName("cvd_1"): {
						Labels: map[string]string{"created_by": "podcvd", "group_name": "cvd_1"},
						Ports:  []PortBinding{{IP: "192.168.80.1", PrivatePort: 1443}},
					},
					ContainerName("cvd_2"): {
						Labels: map[string]string{"created_by": "podcvd", "group_name": "cvd_2"},
						Ports:  []PortBinding{{IP: "192.168.80.2", PrivatePort: 1443}},
					},
				},
				execResults: map[string]execResult{
					ContainerName("cvd_1"): {
						wantCmd: []string{"cvd", "ps"},
						output:  "GROUP   STATUS\ncvd_1   Running\n",
					},
					ContainerName("cvd_2"): {
						wantCmd: []string{"cvd", "ps"},
						output:  "GROUP   STATUS\ncvd_2   Stopped\n",
					},
				},
			},
			subcommandArgs: []string{"cvd", "ps"},
			want: []HostExecResult{
				{
					GroupName: "cvd_1",
					IP:        "192.168.80.1",
					Stdout:    []byte("GROUP   STATUS\ncvd_1   Running\n"),
				},
				{
					GroupName: "cvd_2",
					IP:        "192.168.80.2",
					Stdout:    []byte("GROUP   STATUS\ncvd_2   Stopped\n"),
				},
			},
		},
		{
			name: "failure",
			ccm: hostManager{
				containers: map[string]ContainerListEntry{
					ContainerName("cvd_1"): {
						Labels: map[string]string{"created_by": "podcvd", "group_name": "cvd_1"},
						Ports:  []PortBinding{{IP: "192.168.80.1", PrivatePort: 1443}},
					},
				},
				execResults: map[string]execResult{ContainerName("cvd_1"): {fail: true}},
			},
			subcommandArgs: []string{"cvd", "ps"},
			wantErr:        true,
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			got, err := ExecOnAllCuttlefishHosts(tc.ccm, tc.subcommandArgs, io.Discard)
			if tc.wantErr {
				if err == nil {
					t.Errorf("ExecOnAllCuttlefishHosts(%+v, %q) succeeded; want error",
						tc.ccm, tc.subcommandArgs)
				}
				return
			}
			if err != nil {
				t.Fatalf("ExecOnAllCuttlefishHosts(%+v, %q) failed: %v",
					tc.ccm, tc.subcommandArgs, err)
			}
			if !reflect.DeepEqual(got, tc.want) {
				t.Errorf("ExecOnAllCuttlefishHosts(%+v, %q) = %+v; want %+v",
					tc.ccm, tc.subcommandArgs, got, tc.want)
			}
		})
	}
}
