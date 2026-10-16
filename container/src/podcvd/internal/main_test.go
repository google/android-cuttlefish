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
	"context"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"slices"
	"strings"
	"testing"
)

type fakeContainerManager struct {
	execFn   func(ctr string, cmd []string, stderr io.Writer) error
	lastExec []string
}

func (f *fakeContainerManager) ImageExists(ctx context.Context, name string) (bool, error) {
	return true, nil
}

func (f *fakeContainerManager) PullImage(ctx context.Context, name string) error {
	return nil
}

func (f *fakeContainerManager) ListTags(ctx context.Context, imageRepo string) ([]string, error) {
	return nil, nil
}

func (f *fakeContainerManager) RemoveImages(ctx context.Context, images []string) error {
	return nil
}

func (f *fakeContainerManager) ContainerExists(ctx context.Context, name string) (bool, error) {
	return true, nil
}

func (f *fakeContainerManager) InspectContainer(ctx context.Context, name string) (*ContainerInfo, error) {
	return &ContainerInfo{
		Config: &ContainerConfig{
			Labels: map[string]string{
				labelAttemptID: "test-attempt",
			},
		},
	}, nil
}

func (f *fakeContainerManager) CreateAndStartContainer(ctx context.Context, image string, extraFlags []string, name string) (string, error) {
	return "ctr-id", nil
}

func (f *fakeContainerManager) ListContainers(ctx context.Context, all bool) ([]ContainerListEntry, error) {
	return nil, nil
}

func (f *fakeContainerManager) CopyFromContainer(ctx context.Context, ctr string, srcPath string, dstPath string) error {
	return nil
}

func (f *fakeContainerManager) CopyInUserNamespace(ctx context.Context, srcPath string, dstPath string) error {
	return nil
}

func (f *fakeContainerManager) ExecOnContainer(ctx context.Context, ctr string, cmd []string, stdin io.Reader, stdout io.Writer, stderr io.Writer) error {
	f.lastExec = append([]string(nil), cmd...)
	if f.execFn != nil {
		return f.execFn(ctr, cmd, stderr)
	}
	return nil
}

func (f *fakeContainerManager) CommitContainer(ctx context.Context, ctr string, image string) error {
	return nil
}

func (f *fakeContainerManager) StopAndRemoveContainer(ctx context.Context, ctr string) error {
	return nil
}

func TestHandleSnapshotTakeExecution(t *testing.T) {
	snapshotsDir := t.TempDir()
	hostDestDir := filepath.Join(t.TempDir(), "out_snapshot")

	failExec := false
	ccm := &fakeContainerManager{
		execFn: func(ctr string, cmd []string, stderr io.Writer) error {
			if failExec {
				return fmt.Errorf("simulated container snapshot_take failure")
			}
			var containerSnapPath string
			for _, arg := range cmd {
				if v, ok := strings.CutPrefix(arg, "-snapshot_path="); ok {
					containerSnapPath = v
				} else if v, ok := strings.CutPrefix(arg, "--snapshot_path="); ok {
					containerSnapPath = v
				}
			}
			if !strings.HasPrefix(containerSnapPath, "/podcvd_snapshot/") {
				t.Fatalf("unexpected container snapshot_path %q in cmd %v", containerSnapPath, cmd)
			}
			rel := strings.TrimPrefix(containerSnapPath, "/podcvd_snapshot/")
			hostStageDir := filepath.Join(snapshotsDir, rel)
			// Simulate snapshot_util_cvd's native --force check on the staging directory.
			if _, err := os.Stat(hostStageDir); err == nil {
				if !slices.Contains(cmd, "--force") {
					if stderr != nil {
						_, _ = fmt.Fprintf(stderr, "Delete the destination directory %q first\n", containerSnapPath)
					}
					return fmt.Errorf("snapshot path %q cannot exist unless --force is given", containerSnapPath)
				}
				if err := os.RemoveAll(hostStageDir); err != nil {
					return err
				}
			}
			if err := os.MkdirAll(hostStageDir, 0755); err != nil {
				return err
			}
			return os.WriteFile(filepath.Join(hostStageDir, "marker.txt"), []byte("ok"), 0644)
		},
	}

	cvdArgs, err := ParseCvdArgs([]string{"--group_name=cvd_1", "snapshot_take", "--snapshot_path=" + hostDestDir})
	if err != nil {
		t.Fatalf("ParseCvdArgs failed: %v", err)
	}
	if err := handleSnapshotTakeExecution(ccm, cvdArgs, snapshotsDir); err != nil {
		t.Fatalf("handleSnapshotTakeExecution failed: %v", err)
	}
	fi, err := os.Lstat(hostDestDir)
	if err != nil {
		t.Fatalf("expected symlink at %s: %v", hostDestDir, err)
	}
	if fi.Mode()&os.ModeSymlink == 0 {
		t.Fatalf("expected %s to be a symlink, got mode %v", hostDestDir, fi.Mode())
	}
	firstTarget, err := os.Readlink(hostDestDir)
	if err != nil {
		t.Fatalf("Readlink(%s) failed: %v", hostDestDir, err)
	}
	if _, err := os.Stat(filepath.Join(hostDestDir, "marker.txt")); err != nil {
		t.Fatalf("expected snapshot marker via symlink at %s: %v", hostDestDir, err)
	}

	// Second call without --force must fail because hostDestDir exists and pre-creates hostStagingPath.
	cvdArgsNoForce, err := ParseCvdArgs([]string{"--group_name=cvd_1", "snapshot_take", "--snapshot_path=" + hostDestDir})
	if err != nil {
		t.Fatalf("ParseCvdArgs failed: %v", err)
	}
	if err := handleSnapshotTakeExecution(ccm, cvdArgsNoForce, snapshotsDir); err == nil {
		t.Fatalf("expected error when snapshot_path already exists without --force")
	}

	// A failing call with --force must not delete the pre-existing hostDestDir or its target.
	failExec = true
	cvdArgsForceFail, err := ParseCvdArgs([]string{"--group_name=cvd_1", "snapshot_take", "--force", "--snapshot_path=" + hostDestDir})
	if err != nil {
		t.Fatalf("ParseCvdArgs failed: %v", err)
	}
	if err := handleSnapshotTakeExecution(ccm, cvdArgsForceFail, snapshotsDir); err == nil {
		t.Fatalf("expected error when container snapshot_take fails")
	}
	if _, err := os.Stat(filepath.Join(hostDestDir, "marker.txt")); err != nil {
		t.Fatalf("expected pre-existing snapshot to remain intact when snapshot_take --force fails: %v", err)
	}

	// Succeeding call with --force must update hostDestDir symlink and remove the superseded snapshot target.
	failExec = false
	cvdArgsForce, err := ParseCvdArgs([]string{"--group_name=cvd_1", "snapshot_take", "--force", "--snapshot_path=" + hostDestDir})
	if err != nil {
		t.Fatalf("ParseCvdArgs failed: %v", err)
	}
	if err := handleSnapshotTakeExecution(ccm, cvdArgsForce, snapshotsDir); err != nil {
		t.Fatalf("handleSnapshotTakeExecution with --force failed: %v", err)
	}
	secondTarget, err := os.Readlink(hostDestDir)
	if err != nil || secondTarget == firstTarget {
		t.Fatalf("expected updated symlink target, got %q (first=%q, err=%v)", secondTarget, firstTarget, err)
	}
	if _, err := os.Stat(firstTarget); !os.IsNotExist(err) {
		t.Fatalf("expected superseded snapshot dir %q to be removed, stat err = %v", firstTarget, err)
	}
	if _, err := os.Stat(filepath.Join(hostDestDir, "marker.txt")); err != nil {
		t.Fatalf("expected marker.txt via updated symlink at %s: %v", hostDestDir, err)
	}
}

func TestResolveSnapshotForRestore(t *testing.T) {
	snapshotsDir := t.TempDir()
	snap1Target := filepath.Join(snapshotsDir, "uuid-1")
	snap2Target := filepath.Join(snapshotsDir, "uuid-2")
	for _, dir := range []string{snap1Target, snap2Target} {
		if err := os.MkdirAll(dir, 0755); err != nil {
			t.Fatal(err)
		}
	}
	hostSnap1 := filepath.Join(t.TempDir(), "snap1")
	hostSnap2 := filepath.Join(t.TempDir(), "snap2")
	if err := os.Symlink(snap1Target, hostSnap1); err != nil {
		t.Fatal(err)
	}
	if err := os.Symlink(snap2Target, hostSnap2); err != nil {
		t.Fatal(err)
	}

	for _, tc := range []struct {
		hostPath string
		want     string
	}{
		{hostSnap1, "/podcvd_snapshot/uuid-1"},
		{hostSnap2, "/podcvd_snapshot/uuid-2"},
	} {
		cvdArgs, err := ParseCvdArgs([]string{"--group_name=cvd_1", "start", "--snapshot_path=" + tc.hostPath})
		if err != nil {
			t.Fatalf("ParseCvdArgs failed: %v", err)
		}
		if err := resolveSnapshotForRestore(cvdArgs, snapshotsDir); err != nil {
			t.Fatalf("resolveSnapshotForRestore(%q) failed: %v", tc.hostPath, err)
		}
		got, ok := cvdArgs.GetStringFlagValueOnSubCommandArgs("snapshot_path")
		if !ok || got != tc.want {
			t.Fatalf("rewritten snapshot_path = %q (ok=%v), want %q", got, ok, tc.want)
		}
	}

	entries, err := os.ReadDir(snapshotsDir)
	if err != nil || len(entries) != 2 {
		t.Fatalf("expected both snapshots to remain in %s, got %d (err=%v)", snapshotsDir, len(entries), err)
	}
}
