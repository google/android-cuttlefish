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
	"io"
	"os"
	"path/filepath"
	"reflect"
	"slices"
	"strings"
	"testing"
)

func setupFakePodman(t *testing.T, script string) {
	t.Helper()
	dir := t.TempDir()
	if err := os.WriteFile(filepath.Join(dir, "podman"), []byte(script), 0755); err != nil {
		t.Fatal(err)
	}
	t.Setenv("PATH", dir)
}

func TestImageExists(t *testing.T) {
	setupFakePodman(t, `#!/bin/sh
case "$*" in
"image exists localhost/cuttlefish-orchestration:latest") exit 0 ;;
*) exit 1 ;;
esac
`)
	ccm := NewCuttlefishContainerManager()

	tests := []struct {
		name  string
		image string
		want  bool
	}{
		{
			name:  "exists",
			image: "localhost/cuttlefish-orchestration:latest",
			want:  true,
		},
		{
			name:  "not exists",
			image: "localhost/cuttlefish-orchestration:1.57.0",
			want:  false,
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			got, err := ccm.ImageExists(t.Context(), tc.image)
			if err != nil {
				t.Fatalf("ImageExists(%q) failed: %v", tc.image, err)
			}
			if got != tc.want {
				t.Errorf("ImageExists(%q) = %v; want %v", tc.image, got, tc.want)
			}
		})
	}
}

func TestPullImage(t *testing.T) {
	setupFakePodman(t, `#!/bin/sh
case "$*" in
"pull localhost/cuttlefish-orchestration:1.58.0") exit 0 ;;
*) exit 1 ;;
esac
`)
	ccm := NewCuttlefishContainerManager()

	tests := []struct {
		name    string
		image   string
		wantErr bool
	}{
		{
			name:  "pull",
			image: "localhost/cuttlefish-orchestration:1.58.0",
		},
		{
			name:    "failure",
			image:   "localhost/unknown:latest",
			wantErr: true,
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			err := ccm.PullImage(t.Context(), tc.image)
			if gotErr := err != nil; gotErr != tc.wantErr {
				t.Errorf("PullImage(%q) error = %v; want error %v", tc.image, err, tc.wantErr)
			}
		})
	}
}

func TestListTags(t *testing.T) {
	setupFakePodman(t, `#!/bin/sh
case "$*" in
"image list --filter reference=localhost/cuttlefish-orchestration \
--format {{.Tag}}") printf 'nightly\n1.58.0\n' ;;
*) exit 1 ;;
esac
`)
	ccm := NewCuttlefishContainerManager()

	tests := []struct {
		name      string
		imageRepo string
		want      []string
		wantErr   bool
	}{
		{
			name:      "tags",
			imageRepo: "localhost/cuttlefish-orchestration",
			want:      []string{"nightly", "1.58.0"},
		},
		{
			name:      "failure",
			imageRepo: "localhost/unknown",
			wantErr:   true,
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			got, err := ccm.ListTags(t.Context(), tc.imageRepo)
			if tc.wantErr {
				if err == nil {
					t.Errorf("ListTags(%q) succeeded; want error", tc.imageRepo)
				}
				return
			}
			if err != nil {
				t.Fatalf("ListTags(%q) failed: %v", tc.imageRepo, err)
			}
			if !slices.Equal(got, tc.want) {
				t.Errorf("ListTags(%q) = %q; want %q", tc.imageRepo, got, tc.want)
			}
		})
	}
}

func TestRemoveImages(t *testing.T) {
	setupFakePodman(t, `#!/bin/sh
case "$*" in
"image rm -i \
localhost/cuttlefish-orchestration:1.57.0 \
localhost/cuttlefish-orchestration:1.56.0") exit 0 ;;
"image rm -i localhost/cuttlefish-orchestration:1.55.0") exit 2 ;;
*) exit 1 ;;
esac
`)
	ccm := NewCuttlefishContainerManager()

	tests := []struct {
		name    string
		images  []string
		wantErr bool
	}{
		{
			name: "remove",
			images: []string{
				"localhost/cuttlefish-orchestration:1.57.0",
				"localhost/cuttlefish-orchestration:1.56.0",
			},
		},
		{
			name:   "image in use",
			images: []string{"localhost/cuttlefish-orchestration:1.55.0"},
		},
		{
			name:   "no images",
			images: nil,
		},
		{
			name:    "failure",
			images:  []string{"localhost/cuttlefish-orchestration:1.54.0"},
			wantErr: true,
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			err := ccm.RemoveImages(t.Context(), tc.images)
			if gotErr := err != nil; gotErr != tc.wantErr {
				t.Errorf("RemoveImages(%q) error = %v; want error %v", tc.images, err, tc.wantErr)
			}
		})
	}
}

func TestContainerExists(t *testing.T) {
	setupFakePodman(t, `#!/bin/sh
case "$*" in
"container exists d10f7d5f996b") exit 0 ;;
*) exit 1 ;;
esac
`)
	ccm := NewCuttlefishContainerManager()

	tests := []struct {
		name      string
		container string
		want      bool
	}{
		{
			name:      "exists",
			container: "d10f7d5f996b",
			want:      true,
		},
		{
			name:      "not exists",
			container: "7dafbed2fa09",
			want:      false,
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			got, err := ccm.ContainerExists(t.Context(), tc.container)
			if err != nil {
				t.Fatalf("ContainerExists(%q) failed: %v", tc.container, err)
			}
			if got != tc.want {
				t.Errorf("ContainerExists(%q) = %v; want %v", tc.container, got, tc.want)
			}
		})
	}
}

func TestInspectContainer(t *testing.T) {
	setupFakePodman(t, `#!/bin/sh
case "$*" in
"inspect d10f7d5f996b") echo '[{"Config":{"Labels":{
  "group_name":"cvd_1",
  "base_dir":"/home/user/.podcvd/cvd_1"
}}}]' ;;
"inspect 7dafbed2fa09") echo '[]' ;;
"inspect 64331ba99c36") echo 'not json' ;;
*) exit 1 ;;
esac
`)
	ccm := NewCuttlefishContainerManager()

	tests := []struct {
		name      string
		container string
		want      *ContainerInfo
		wantErr   bool
	}{
		{
			name:      "inspect",
			container: "d10f7d5f996b",
			want: &ContainerInfo{
				Config: &ContainerConfig{
					Labels: map[string]string{
						"group_name": "cvd_1",
						"base_dir":   "/home/user/.podcvd/cvd_1",
					},
				},
			},
		},
		{
			name:      "no info",
			container: "7dafbed2fa09",
			wantErr:   true,
		},
		{
			name:      "invalid json",
			container: "64331ba99c36",
			wantErr:   true,
		},
		{
			name:      "failure",
			container: "8de032689c0e",
			wantErr:   true,
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			got, err := ccm.InspectContainer(t.Context(), tc.container)
			if tc.wantErr {
				if err == nil {
					t.Errorf("InspectContainer(%q) succeeded; want error", tc.container)
				}
				return
			}
			if err != nil {
				t.Fatalf("InspectContainer(%q) failed: %v", tc.container, err)
			}
			if !reflect.DeepEqual(got, tc.want) {
				t.Errorf("InspectContainer(%q) = %+v; want %+v", tc.container, got, tc.want)
			}
		})
	}
}

func TestCreateAndStartContainer(t *testing.T) {
	setupFakePodman(t, `#!/bin/sh
case "$*" in
"run -d -t --rm --cap-add NET_ADMIN \
--device /dev/kvm:/dev/kvm:rwm \
--device /dev/net/tun:/dev/net/tun:rwm \
--device /dev/vhost-net:/dev/vhost-net:rwm \
--device /dev/vhost-vsock:/dev/vhost-vsock:rwm \
-e CVD_INVOKER=podcvd "*"--label created_by=podcvd \
--label group_name=cvd_1 \
--name d10f7d5f996b \
`+imageName+`") echo 3f2a9c1b7d4e ;;
"run -d -t --rm --cap-add NET_ADMIN \
--device /dev/kvm:/dev/kvm:rwm \
--device /dev/net/tun:/dev/net/tun:rwm \
--device /dev/vhost-net:/dev/vhost-net:rwm \
--device /dev/vhost-vsock:/dev/vhost-vsock:rwm \
-e CVD_INVOKER=podcvd "*"--label created_by=podcvd \
`+imageName+`") echo 9b8e7f6a5c4d ;;
*) exit 1 ;;
esac
`)
	ccm := NewCuttlefishContainerManager()

	tests := []struct {
		name       string
		extraFlags []string
		container  string
		want       string
		wantErr    bool
	}{
		{
			name:       "with name",
			extraFlags: []string{"--label", "created_by=podcvd", "--label", "group_name=cvd_1"},
			container:  "d10f7d5f996b",
			want:       "3f2a9c1b7d4e",
		},
		{
			name:       "without name",
			extraFlags: []string{"--label", "created_by=podcvd"},
			container:  "",
			want:       "9b8e7f6a5c4d",
		},
		{
			name:       "failure",
			extraFlags: []string{"--label", "created_by=podcvd"},
			container:  "tooling",
			wantErr:    true,
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			got, err := ccm.CreateAndStartContainer(t.Context(), tc.extraFlags, tc.container)
			if tc.wantErr {
				if err == nil {
					t.Errorf("CreateAndStartContainer(%q, %q) succeeded; want error",
						tc.extraFlags, tc.container)
				}
				return
			}
			if err != nil {
				t.Fatalf("CreateAndStartContainer(%q, %q) failed: %v",
					tc.extraFlags, tc.container, err)
			}
			if got != tc.want {
				t.Errorf("CreateAndStartContainer(%q, %q) = %q; want %q",
					tc.extraFlags, tc.container, got, tc.want)
			}
		})
	}
}

func TestListContainers(t *testing.T) {
	setupFakePodman(t, `#!/bin/sh
case "$*" in
"ps --filter label=group_name --filter label=created_by=podcvd \
--format json") echo '[
  {
    "Id":"3f2a9c1b7d4e",
    "Names":["d10f7d5f996b"],
    "State":"running",
    "Labels":{"created_by":"podcvd","group_name":"cvd_1"},
    "Ports":[
      {"host_ip":"192.168.80.1","container_port":1443,"host_port":11443,"protocol":"tcp"},
      {"host_ip":"192.168.80.1","container_port":6520,"host_port":6520,"protocol":"tcp"}
    ]
  }
]' ;;
"ps --filter label=group_name --filter label=created_by=podcvd \
--format json --all") echo '[
  {
    "Id":"3f2a9c1b7d4e",
    "Names":["d10f7d5f996b"],
    "State":"running",
    "Labels":{"created_by":"podcvd","group_name":"cvd_1"},
    "Ports":[
      {"host_ip":"192.168.80.1","container_port":1443,"host_port":11443,"protocol":"tcp"},
      {"host_ip":"192.168.80.1","container_port":6520,"host_port":6520,"protocol":"tcp"}
    ]
  },
  {
    "Id":"9b8e7f6a5c4d",
    "Names":["7dafbed2fa09"],
    "State":"created",
    "Labels":{"created_by":"podcvd","group_name":"cvd_2"},
    "Ports":[
      {"host_ip":"192.168.80.2","container_port":1443,"host_port":11443,"protocol":"tcp"}
    ]
  }
]' ;;
*) exit 1 ;;
esac
`)
	ccm := NewCuttlefishContainerManager()

	tests := []struct {
		name string
		all  bool
		want []ContainerListEntry
	}{
		{
			name: "running containers",
			all:  false,
			want: []ContainerListEntry{
				{
					Labels: map[string]string{"created_by": "podcvd", "group_name": "cvd_1"},
					Ports: []PortBinding{
						{IP: "192.168.80.1", PrivatePort: 1443},
						{IP: "192.168.80.1", PrivatePort: 6520},
					},
				},
			},
		},
		{
			name: "all containers",
			all:  true,
			want: []ContainerListEntry{
				{
					Labels: map[string]string{"created_by": "podcvd", "group_name": "cvd_1"},
					Ports: []PortBinding{
						{IP: "192.168.80.1", PrivatePort: 1443},
						{IP: "192.168.80.1", PrivatePort: 6520},
					},
				},
				{
					Labels: map[string]string{"created_by": "podcvd", "group_name": "cvd_2"},
					Ports:  []PortBinding{{IP: "192.168.80.2", PrivatePort: 1443}},
				},
			},
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			got, err := ccm.ListContainers(t.Context(), tc.all)
			if err != nil {
				t.Fatalf("ListContainers(%v) failed: %v", tc.all, err)
			}
			if !reflect.DeepEqual(got, tc.want) {
				t.Errorf("ListContainers(%v) = %+v; want %+v", tc.all, got, tc.want)
			}
		})
	}
}

func TestCopyFromContainer(t *testing.T) {
	setupFakePodman(t, `#!/bin/sh
case "$*" in
"cp d10f7d5f996b:/tmp/host_bugreport.zip /home/user/host_bugreport.zip") exit 0 ;;
*) exit 1 ;;
esac
`)
	ccm := NewCuttlefishContainerManager()

	tests := []struct {
		name      string
		container string
		srcPath   string
		dstPath   string
		wantErr   bool
	}{
		{
			name:      "copy",
			container: "d10f7d5f996b",
			srcPath:   "/tmp/host_bugreport.zip",
			dstPath:   "/home/user/host_bugreport.zip",
		},
		{
			name:      "failure",
			container: "7dafbed2fa09",
			srcPath:   "/tmp/missing.zip",
			dstPath:   "/home/user/missing.zip",
			wantErr:   true,
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			err := ccm.CopyFromContainer(t.Context(), tc.container, tc.srcPath, tc.dstPath)
			if gotErr := err != nil; gotErr != tc.wantErr {
				t.Errorf("CopyFromContainer(%q, %q, %q) error = %v; want error %v",
					tc.container, tc.srcPath, tc.dstPath, err, tc.wantErr)
			}
		})
	}
}

func TestExecOnContainer(t *testing.T) {
	setupFakePodman(t, `#!/bin/sh
case "$*" in
"exec d10f7d5f996b cvd fleet") echo '{"groups":[]}' ;;
"exec -i 7dafbed2fa09 cvd remove") exit 0 ;;
*) exit 1 ;;
esac
`)
	ccm := NewCuttlefishContainerManager()

	tests := []struct {
		name       string
		container  string
		cmd        []string
		stdin      io.Reader
		wantStdout string
		wantErr    bool
	}{
		{
			name:       "without stdin",
			container:  "d10f7d5f996b",
			cmd:        []string{"cvd", "fleet"},
			stdin:      nil,
			wantStdout: "{\"groups\":[]}\n",
		},
		{
			name:       "with stdin",
			container:  "7dafbed2fa09",
			cmd:        []string{"cvd", "remove"},
			stdin:      strings.NewReader("input from host stdin\n"),
			wantStdout: "",
		},
		{
			name:      "failure",
			container: "8de032689c0e",
			cmd:       []string{"cvd", "start"},
			stdin:     nil,
			wantErr:   true,
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			var stdout strings.Builder
			err := ccm.ExecOnContainer(
				t.Context(), tc.container, tc.cmd, tc.stdin, &stdout, nil)
			if tc.wantErr {
				if err == nil {
					t.Errorf("ExecOnContainer(%q, %q) succeeded; want error", tc.container, tc.cmd)
				}
				return
			}
			if err != nil {
				t.Fatalf("ExecOnContainer(%q, %q) failed: %v", tc.container, tc.cmd, err)
			}
			if got := stdout.String(); got != tc.wantStdout {
				t.Errorf("ExecOnContainer(%q, %q) stdout = %q; want %q",
					tc.container, tc.cmd, got, tc.wantStdout)
			}
		})
	}
}

func TestStopAndRemoveContainer(t *testing.T) {
	setupFakePodman(t, `#!/bin/sh
case "$*" in
"rm -f -i -t 0 d10f7d5f996b") exit 0 ;;
*) exit 1 ;;
esac
`)
	ccm := NewCuttlefishContainerManager()

	tests := []struct {
		name      string
		container string
		wantErr   bool
	}{
		{
			name:      "remove",
			container: "d10f7d5f996b",
		},
		{
			name:      "failure",
			container: "7dafbed2fa09",
			wantErr:   true,
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			err := ccm.StopAndRemoveContainer(t.Context(), tc.container)
			if gotErr := err != nil; gotErr != tc.wantErr {
				t.Errorf("StopAndRemoveContainer(%q) error = %v; want error %v",
					tc.container, err, tc.wantErr)
			}
		})
	}
}

func TestCuttlefishContainerManagerWithoutPodman(t *testing.T) {
	t.Setenv("PATH", t.TempDir())
	ccm := NewCuttlefishContainerManager()

	tests := []struct {
		name string
		call func() error
	}{
		{
			name: "image exists",
			call: func() error {
				_, err := ccm.ImageExists(t.Context(), "localhost/cuttlefish-orchestration:latest")
				return err
			},
		},
		{
			name: "container exists",
			call: func() error {
				_, err := ccm.ContainerExists(t.Context(), "d10f7d5f996b")
				return err
			},
		},
		{
			name: "list containers",
			call: func() error {
				_, err := ccm.ListContainers(t.Context(), true)
				return err
			},
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			if err := tc.call(); err == nil {
				t.Errorf("%s succeeded without podman; want error", tc.name)
			}
		})
	}
}
