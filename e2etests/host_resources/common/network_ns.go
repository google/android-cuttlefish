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
	"os"
	"os/exec"
	"strings"
	"testing"
)

func checkNetworkPrereqs(t *testing.T) {
	t.Helper()
	ensurePath()
	for _, bin := range []string{"ip", "nft"} {
		if _, err := exec.LookPath(bin); err != nil {
			t.Fatalf("required binary %q not found on PATH: %v", bin, err)
		}
	}
	if _, err := os.Stat("/dev/net/tun"); err != nil {
		t.Fatalf("/dev/net/tun is not available: %v", err)
	}
}

// make sure the sbin directories that hold ip and nft are on PATH
func ensurePath() {
	parts := strings.Split(os.Getenv("PATH"), ":")
	have := map[string]bool{}
	for _, p := range parts {
		have[p] = true
	}
	for _, e := range []string{"/usr/sbin", "/usr/bin", "/sbin", "/bin"} {
		if !have[e] {
			parts = append(parts, e)
		}
	}
	os.Setenv("PATH", strings.Join(parts, ":"))
}

func (s *Sandbox) setupNetwork() error {
	_, err := s.Run("ip", "link", "set", "lo", "up")
	return err
}
