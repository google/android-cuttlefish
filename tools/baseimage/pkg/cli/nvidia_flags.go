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
	"flag"
	"strings"

	"github.com/google/android-cuttlefish/tools/baseimage/pkg/gce"
	"github.com/google/android-cuttlefish/tools/baseimage/pkg/nvidia"
)

// LabelNvidiaVersion carries the validated NVIDIA driver version from Step 1 to Step 2.
const LabelNvidiaVersion = "cuttlefish-nvidia-version"

// NvidiaFlags holds the shared NVIDIA driver selection flags.
type NvidiaFlags struct {
	Version string
	Branch  string
}

// Register binds the NVIDIA driver flags to fs.
func (f *NvidiaFlags) Register(fs *flag.FlagSet) {
	fs.StringVar(&f.Version, "nvidia-version", "", "NVIDIA Tesla driver version (e.g. 580.196.11). Mutually exclusive with -nvidia-branch.")
	fs.StringVar(&f.Branch, "nvidia-branch", nvidia.BranchGCPLTS, "NVIDIA driver branch: gcp-lts (GCP recommended LTS), lts, or a major branch number (e.g. 580, 620). Mutually exclusive with -nvidia-version.")
}

// AnyExplicit reports whether any -nvidia-* flag was explicitly passed.
func (f *NvidiaFlags) AnyExplicit(fs *flag.FlagSet) bool {
	return flagExplicit(fs, "nvidia-version") || flagExplicit(fs, "nvidia-branch")
}

func flagExplicit(fs *flag.FlagSet, name string) bool {
	found := false
	fs.Visit(func(fl *flag.Flag) {
		if fl.Name == name {
			found = true
		}
	})
	return found
}

// ToResolveOpts validates the flag values and converts them to nvidia.ResolveOpts.
func (f *NvidiaFlags) ToResolveOpts(fs *flag.FlagSet, arch gce.Arch) (nvidia.ResolveOpts, error) {
	opts := nvidia.ResolveOpts{
		Arch:           arch,
		Version:        strings.TrimSpace(f.Version),
		Branch:         strings.TrimSpace(f.Branch),
		BranchExplicit: flagExplicit(fs, "nvidia-branch"),
	}
	if err := nvidia.ValidateOpts(opts); err != nil {
		return nvidia.ResolveOpts{}, err
	}
	return opts, nil
}

// NvidiaLabels returns GCE labels describing the validated NVIDIA driver.
func NvidiaLabels(d *nvidia.Driver) map[string]string {
	if d == nil {
		return nil
	}
	return map[string]string{
		LabelNvidiaVersion: strings.ReplaceAll(d.Version, ".", "_"),
	}
}

// NvidiaVersionFromLabels returns the stamped NVIDIA driver version label, if present.
func NvidiaVersionFromLabels(labels map[string]string) (string, bool) {
	raw := labels[LabelNvidiaVersion]
	if raw == "" {
		return "", false
	}
	return strings.ReplaceAll(raw, "_", "."), true
}
