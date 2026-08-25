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
	"log"
	"os"
	"path/filepath"
	"strings"
	"time"
)

// cvdallocBuiltRelPath is where `bazel build` in the base/cvd module drops the
// cvdalloc binary, relative to the repository root.
const cvdallocBuiltRelPath = "base/cvd/bazel-bin/cuttlefish/host/commands/cvdalloc/cvdalloc"

// resolveCvdallocBin returns the cvdalloc binary to test, honoring $CVDALLOC_BIN
// as an override and otherwise auto-discovering the locally-built binary.
func resolveCvdallocBin() (string, error) {
	if bin := os.Getenv("CVDALLOC_BIN"); bin != "" {
		fi, err := os.Stat(bin)
		if err != nil {
			return "", fmt.Errorf("CVDALLOC_BIN=%q is not usable: %v", bin, err)
		}
		if fi.IsDir() {
			return "", fmt.Errorf("CVDALLOC_BIN=%q is a directory, expected a binary", bin)
		}
		return bin, nil
	}

	root, err := findRepoRoot()
	if err != nil {
		return "", fmt.Errorf("could not locate the repository root to find the "+
			"locally-built cvdalloc: %v\nBuild it and retry:\n"+
			"    (cd base/cvd && bazel build //cuttlefish/host/commands/cvdalloc:cvdalloc)\n"+
			"or set CVDALLOC_BIN to a specific binary.", err)
	}
	cand := filepath.Join(root, cvdallocBuiltRelPath)
	fi, err := os.Stat(cand)
	if err != nil || fi.IsDir() {
		return "", fmt.Errorf("no locally-built cvdalloc found at %s\nBuild it first:\n"+
			"    (cd base/cvd && bazel build //cuttlefish/host/commands/cvdalloc:cvdalloc)\n"+
			"or set CVDALLOC_BIN to a specific binary.", cand)
	}
	return cand, nil
}

// findRepoRoot locates the android-cuttlefish checkout root non-hermetically.
//
// Under `bazel test` the working directory lives deep inside the bazel cache at
// .../execroot/_main/..., and the top-level entries of execroot/_main are
// symlinks back into the real source workspace. We resolve one of those symlinks 
// to recover the source tree, then walk up to the repo root.
func findRepoRoot() (string, error) {
	if d := os.Getenv("BUILD_WORKSPACE_DIRECTORY"); d != "" {
		if r, ok := walkUpForRepoRoot(d); ok {
			return r, nil
		}
	}

	wd, wdErr := os.Getwd()
	if wdErr == nil {
		if r, ok := repoRootFromExecroot(wd); ok {
			return r, nil
		}
	}

	var anchors []string
	if wdErr == nil {
		anchors = append(anchors, wd)
	}
	if exe, err := os.Executable(); err == nil {
		if real, err := filepath.EvalSymlinks(exe); err == nil {
			anchors = append(anchors, real)
		}
		anchors = append(anchors, exe)
	}
	for _, a := range anchors {
		if r, ok := walkUpForRepoRoot(a); ok {
			return r, nil
		}
	}
	return "", fmt.Errorf("could not resolve repo root from cwd %q", wd)
}

// repoRootFromExecroot recovers the source workspace from a path inside the
// bazel execroot by following the symlink forest under execroot/_main.
func repoRootFromExecroot(path string) (string, bool) {
	const marker = "/execroot/_main/"
	i := strings.Index(path, marker)
	if i < 0 {
		return "", false
	}
	execMain := path[:i] + "/execroot/_main"
	// Any of these top-level entries of the e2etests module is a symlink into
	// the source tree; resolving it lands us in <repo>/e2etests.
	for _, name := range []string{"MODULE.bazel", "WORKSPACE", "host_resources", "go.mod"} {
		real, err := filepath.EvalSymlinks(filepath.Join(execMain, name))
		if err != nil {
			continue
		}
		if r, ok := walkUpForRepoRoot(real); ok {
			return r, true
		}
	}
	return "", false
}

func walkUpForRepoRoot(start string) (string, bool) {
	dir := start
	if fi, err := os.Stat(dir); err == nil && !fi.IsDir() {
		dir = filepath.Dir(dir)
	}
	for {
		if isRepoRoot(dir) {
			return dir, true
		}
		parent := filepath.Dir(dir)
		if parent == dir {
			return "", false
		}
		dir = parent
	}
}

// isRepoRoot reports whether dir looks like the android-cuttlefish checkout root.
func isRepoRoot(dir string) bool {
	for _, marker := range []string{"base/cvd", "e2etests"} {
		if _, err := os.Stat(filepath.Join(dir, marker)); err != nil {
			return false
		}
	}
	return true
}

// warnIfStale best-effort compares the binary's mtime against cvdalloc's source.
func warnIfStale(bin string, binMtime time.Time) {
	idx := strings.Index(bin, "/bazel-bin/")
	if idx < 0 {
		return
	}
	root := bin[:idx]
	dirs := []string{
		filepath.Join(root, "cuttlefish", "host", "commands", "cvdalloc"),
		filepath.Join(root, "allocd"),
	}
	var newest time.Time
	found := false
	for _, dir := range dirs {
		if mt, ok := newestSourceMtime(dir); ok && mt.After(newest) {
			newest, found = mt, true
		}
	}
	if found && newest.After(binMtime) {
		log.Printf("WARNING: cvdalloc sources appear newer than the binary "+
			"(newest source %s > binary %s); you may be testing a stale build. "+
			"Rebuild with: (cd base/cvd && bazel build //cuttlefish/host/commands/cvdalloc:cvdalloc)",
			newest.Format(time.RFC3339), binMtime.Format(time.RFC3339))
	}
}

func newestSourceMtime(dir string) (time.Time, bool) {
	var newest time.Time
	found := false
	filepath.WalkDir(dir, func(_ string, d os.DirEntry, err error) error {
		if err != nil || d.IsDir() {
			return nil
		}
		switch filepath.Ext(d.Name()) {
		case ".cpp", ".cc", ".h", ".hpp":
			if fi, err := d.Info(); err == nil && fi.ModTime().After(newest) {
				newest, found = fi.ModTime(), true
			}
		}
		return nil
	})
	return newest, found
}
