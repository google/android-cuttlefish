// Copyright (C) 2025 The Android Open Source Project
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
	"fmt"
)

type Arch int

const (
	ArchUnknown Arch = iota
	ArchX86
	ArchArm
)

func ParseArch(s string) (Arch, error) {
	switch s {
	case "x86_64":
		return ArchX86, nil
	case "arm64":
		return ArchArm, nil
	default:
		return ArchUnknown, fmt.Errorf("unknown arch %q", s)
	}
}
