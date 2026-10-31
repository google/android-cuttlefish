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

package gce

import (
	"strings"
	"testing"
)

func TestGceResourceNameWithSuffix(t *testing.T) {
	short := GceResourceNameWithSuffix("cuttlefish-image-20261031015900--cvd1-55", "-attached-disk")
	wantShort := "cuttlefish-image-20261031015900--cvd1-55-attached-disk"
	if short != wantShort {
		t.Errorf("GceResourceNameWithSuffix(short) = %q, want %q", short, wantShort)
	}
	if len(short) > MaxGceResourceNameLen {
		t.Errorf("len(short) = %d, exceeds %d", len(short), MaxGceResourceNameLen)
	}

	longBase := "cuttlefish-image-20261031015900-very-long-suffix--cvd1-55--160gb"
	longGot := GceResourceNameWithSuffix(longBase, "-attached-disk")
	if len(longGot) > MaxGceResourceNameLen {
		t.Errorf("len(longGot) = %d (%q), exceeds %d", len(longGot), longGot, MaxGceResourceNameLen)
	}
	if !strings.HasSuffix(longGot, "-attached-disk") {
		t.Errorf("GceResourceNameWithSuffix(long) = %q, missing -attached-disk suffix", longGot)
	}
	if strings.Contains(longGot, "--attached-disk") {
		t.Errorf("GceResourceNameWithSuffix(long) = %q, did not trim trailing dash before suffix", longGot)
	}
}
