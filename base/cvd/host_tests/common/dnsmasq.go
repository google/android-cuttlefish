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
	_ "embed"
	"os"
	"path/filepath"
	"testing"
)

//go:embed dnsmasq_shim.sh
var dnsmasqShimScript []byte

func installDnsmasqShim(t *testing.T, s *Sandbox) {
	dir := filepath.Join(s.tempdir, "shim")
	if err := os.MkdirAll(dir, 0o755); err != nil {
		t.Fatalf("creating dnsmasq shim dir: %v", err)
	}
	if err := os.WriteFile(filepath.Join(dir, "dnsmasq"), dnsmasqShimScript, 0o755); err != nil {
		t.Fatalf("writing dnsmasq shim: %v", err)
	}
	os.Setenv("PATH", dir+string(os.PathListSeparator)+os.Getenv("PATH"))
}
