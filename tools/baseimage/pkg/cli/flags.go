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

package cli

import (
	"fmt"

	"github.com/google/android-cuttlefish/tools/baseimage/pkg/gce"
)

const (
	archX86 = "x86_64"
	archArm = "arm64"
)

type Arch string

func (a *Arch) String() string {
	return fmt.Sprint(*a)
}

func (a *Arch) Set(value string) error {
	if value != archX86 && value != archArm {
		return fmt.Errorf("unknown arch: %q", value)
	}
	*a = Arch(value)
	return nil
}

func (a *Arch) GceArch() gce.Arch {
	switch string(*a) {
	case "":
		return gce.ArchX86
	case archX86:
		return gce.ArchX86
	case archArm:
		return gce.ArchArm
	default:
		return gce.ArchUnknown
	}
}
