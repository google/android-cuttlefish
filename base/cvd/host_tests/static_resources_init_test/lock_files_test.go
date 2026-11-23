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

package main

import (
	"testing"

	"github.com/google/android-cuttlefish/base/cvd/host_tests/common"
	"github.com/google/go-cmp/cmp"
)

func TestSetupCvdLockFiles(t *testing.T) {
	s := common.NewSandbox(t)
	defer s.Close()
	sr := common.NewStaticResources(t, s)

	grpModeBefore, err := s.Run("stat", "-c", "%a", "/etc/group")
	if err != nil {
		t.Fatalf("stat /etc/group before start: %v", err)
	}

	// Pre-plant a symlink, an active regular lock, a multi-link file, and a
	// nested directory with a symlink inside a 0777 /var/tmp/cvd/lock to
	// verify setup_cvd_lock_files replaces unsafe entries and repairs modes.
	plantCmd := "mkdir -p /var/tmp/cvd/lock/local-instance-4.lock/sub && " +
		"chmod 0777 /var/tmp/cvd /var/tmp/cvd/lock && " +
		"ln -s /etc/group /var/tmp/cvd/lock/local-instance-1.lock && " +
		"printf 'I 1000' > /var/tmp/cvd/lock/local-instance-2.lock && " +
		"chmod 0600 /var/tmp/cvd/lock/local-instance-2.lock && " +
		": > /var/tmp/hardlink_src && " +
		"ln /var/tmp/hardlink_src /var/tmp/cvd/lock/local-instance-3.lock && " +
		"ln -s /etc/group /var/tmp/cvd/lock/local-instance-4.lock/sub/grp"
	if _, err := s.Run("sh", "-c", plantCmd); err != nil {
		t.Fatalf("plant unsafe lock entries: %v", err)
	}

	env := common.InitEnv{NumCvdAccounts: 1}
	if err := sr.Start(env); err != nil {
		t.Fatalf("start: %v", err)
	}
	defer func() {
		if err := sr.Stop(env); err != nil {
			t.Errorf("stop: %v", err)
		}
	}()

	statCmd := []string{
		"stat", "-c", "%F %a %u:%g %h",
		"/var/tmp/cvd",
		"/var/tmp/cvd/lock",
		"/var/tmp/cvd/lock/local-instance-1.lock",
		"/var/tmp/cvd/lock/local-instance-2.lock",
	}
	wantStat := "directory 1777 0:0 3\n" +
		"directory 755 0:0 2\n" +
		"regular empty file 666 0:0 1\n" +
		"regular file 666 0:0 1\n"
	if out, err := s.Run(statCmd...); err != nil {
		t.Errorf("s.Run(%v) failed: %v", statCmd, err)
	} else if diff := cmp.Diff(wantStat, out.Stdout); diff != "" {
		t.Errorf("s.Run(%v) mismatch (-want +got):\n%s", statCmd, diff)
	}
	if out, err := s.Run("cat", "/var/tmp/cvd/lock/local-instance-2.lock"); err != nil || out.Stdout != "I 1000" {
		t.Errorf("cat local-instance-2.lock = (%q, %v), want (%q, nil)", out.Stdout, err, "I 1000")
	}
	for _, removed := range []string{
		"/var/tmp/cvd/lock/local-instance-3.lock",
		"/var/tmp/cvd/lock/local-instance-4.lock",
	} {
		if _, err := s.Run("test", "!", "-e", removed); err != nil {
			t.Errorf("expected %s to be removed: %v", removed, err)
		}
	}
	if out, err := s.Run("stat", "-c", "%a", "/etc/group"); err != nil || out.Stdout != grpModeBefore.Stdout {
		t.Errorf("stat /etc/group = (%q, %v), want (%q, nil)", out.Stdout, err, grpModeBefore.Stdout)
	}
}

func TestSetupCvdLockFilesDynamicAllocation(t *testing.T) {
	s := common.NewSandbox(t)
	defer s.Close()
	sr := common.NewStaticResources(t, s)

	if _, err := s.Run("sh", "-c", "printf 'allocate_static_resources=0\n' > /etc/default/cuttlefish-host-resources"); err != nil {
		t.Fatalf("write /etc/default/cuttlefish-host-resources: %v", err)
	}

	env := common.InitEnv{NumCvdAccounts: 1}
	if err := sr.Start(env); err != nil {
		t.Fatalf("start: %v", err)
	}
	defer func() {
		if err := sr.Stop(env); err != nil {
			t.Errorf("stop: %v", err)
		}
	}()

	statCmd := []string{
		"stat", "-c", "%F %a %u:%g %h",
		"/var/tmp/cvd/lock/local-instance-1.lock",
		"/var/tmp/cvd/lock/local-instance-63.lock",
	}
	wantStat := "regular empty file 666 0:0 1\nregular empty file 666 0:0 1\n"
	if out, err := s.Run(statCmd...); err != nil {
		t.Errorf("s.Run(%v) failed: %v", statCmd, err)
	} else if diff := cmp.Diff(wantStat, out.Stdout); diff != "" {
		t.Errorf("s.Run(%v) mismatch (-want +got):\n%s", statCmd, diff)
	}
	if _, err := s.Run("test", "!", "-e", "/var/tmp/cvd/lock/local-instance-64.lock"); err != nil {
		t.Errorf("expected local-instance-64.lock not to exist: %v", err)
	}
}
