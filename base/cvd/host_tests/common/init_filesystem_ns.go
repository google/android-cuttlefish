// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package common

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

func (s *Sandbox) setupFilesystem() error {
	nsPath := filepath.Join(s.tempdir, "nsswitch.conf")
	if err := os.WriteFile(nsPath, []byte(constructSandboxNsswitch()), 0644); err != nil {
		return fmt.Errorf("writing nsswitch override: %w", err)
	}
	grpPath := filepath.Join(s.tempdir, "group")
	if err := os.WriteFile(grpPath, []byte(constructSandboxGroupFile()), 0644); err != nil {
		return fmt.Errorf("writing group override: %w", err)
	}
	script := fmt.Sprintf(
		"set -e; "+
			"mount --bind %q /etc/nsswitch.conf; "+
			"mount --bind %q /etc/group; "+
			"mount -t tmpfs tmpfs /etc/default; "+
			": > /etc/default/cuttlefish-host-resources; "+
			"mount -t tmpfs tmpfs /run",
		nsPath, grpPath)
	_, err := s.Run("sh", "-c", script)
	return err
}

// reconstruct the host's nsswitch file to use only the group file
func constructSandboxNsswitch() string {
	b, err := os.ReadFile("/etc/nsswitch.conf")
	if err != nil {
		return "passwd: files\ngroup: files\n"
	}
	lines := strings.Split(string(b), "\n")
	found := false
	for i, l := range lines {
		if strings.HasPrefix(strings.TrimSpace(l), "group:") {
			lines[i] = "group: files"
			found = true
		}
	}
	if !found {
		lines = append(lines, "group: files")
	}
	return strings.Join(lines, "\n") + "\n"
}

// reconstruct the host's group file, but with cvdnetwork as gid 0,
// mostly for convenience
func constructSandboxGroupFile() string {
	b, _ := os.ReadFile("/etc/group")
	var out []string
	for _, l := range strings.Split(string(b), "\n") {
		if l == "" || strings.HasPrefix(l, "cvdnetwork:") {
			continue
		}
		out = append(out, l)
	}
	out = append(out, "cvdnetwork:x:0:")
	return strings.Join(out, "\n") + "\n"
}
