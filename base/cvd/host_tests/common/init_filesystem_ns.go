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
	// dnsmasq drops privileges to nobody:dip after writing its pidfile. In a
	// rootless user namespace setgroups(2) is denied, so dnsmasq exits right
	// after startup and only its pidfile remains. The wrapper runs dnsmasq in
	// debug mode (-d: no fork, no privilege drop), detached, and writes the
	// pidfile itself, so the dnsmasq instances started by the init script
	// really run and stop() really has to kill them. The real binary is
	// bind-mounted to real/dnsmasq so the process name stays "dnsmasq".
	realDir := filepath.Join(s.tempdir, "real")
	if err := os.MkdirAll(realDir, 0755); err != nil {
		return fmt.Errorf("creating %s: %w", realDir, err)
	}
	realDnsmasq := filepath.Join(realDir, "dnsmasq")
	if err := os.WriteFile(realDnsmasq, nil, 0755); err != nil {
		return fmt.Errorf("creating dnsmasq mount point: %w", err)
	}
	wrapper := filepath.Join(s.tempdir, "dnsmasq-wrapper")
	if err := os.WriteFile(wrapper, []byte(fmt.Sprintf(dnsmasqWrapper, realDnsmasq)), 0755); err != nil {
		return fmt.Errorf("writing dnsmasq wrapper: %w", err)
	}
	script := fmt.Sprintf(
		"set -e; "+
			"mount --bind %q /etc/nsswitch.conf; "+
			"mount --bind %q /etc/group; "+
			"mount -t tmpfs tmpfs /etc/default; "+
			": > /etc/default/cuttlefish-host-resources; "+
			"if bin=$(command -v dnsmasq); then mount --bind \"$bin\" %q; mount --bind %q \"$bin\"; fi; "+
			"mount -t tmpfs tmpfs /run",
		nsPath, grpPath, realDnsmasq, wrapper)
	_, err := s.Run("sh", "-c", script)
	return err
}

// dnsmasqWrapper replaces dnsmasq in the sandbox; %[1]q is the real binary.
const dnsmasqWrapper = `#!/bin/sh
pidfile=
for a in "$@"; do
  case "$a" in --pid-file=*) pidfile="${a#--pid-file=}" ;; esac
done
%[1]q -d "$@" </dev/null >/dev/null 2>&1 &
pid=$!
sleep 0.2
kill -0 "$pid" 2>/dev/null || { wait "$pid"; exit $?; }
[ -n "$pidfile" ] && echo "$pid" > "$pidfile"
exit 0
`

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
