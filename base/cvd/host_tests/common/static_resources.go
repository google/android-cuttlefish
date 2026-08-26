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
	"testing"

	"github.com/bazelbuild/rules_go/go/runfiles"
)

type InitEnv struct {
	NumCvdAccounts int
}

type StaticResources struct {
	sandbox    *Sandbox
	initScript string
}

func NewStaticResources(t *testing.T, s *Sandbox) *StaticResources {
	script := os.Getenv("INIT_SCRIPT")
	if script == "" {
		t.Fatal("INIT_SCRIPT env var is not set (expected the host-resources init script runfile)")
	}
	full, err := runfiles.Rlocation(script)
	if err != nil {
		t.Fatalf("failed to locate init script runfile %q: %v", script, err)
	}
	if _, err := os.Stat(full); err != nil {
		t.Fatalf("init script %q does not exist: %v", full, err)
	}
	return &StaticResources{sandbox: s, initScript: full}
}

func (r *StaticResources) Start(ie InitEnv) error {
	return r.run("start", ie)
}

func (r *StaticResources) Stop(ie InitEnv) error {
	return r.run("stop", ie)
}

func (r *StaticResources) run(action string, ie InitEnv) error {
	args := []string{"env"}
	if ie.NumCvdAccounts > 0 {
		args = append(args, fmt.Sprintf("num_cvd_accounts=%d", ie.NumCvdAccounts))
	}
	args = append(args, "sh", r.initScript, action)
	_, err := r.sandbox.Run(args...)
	return err
}
