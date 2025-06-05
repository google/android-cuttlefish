// Copyright 2025 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package orchestrator

import (
	"testing"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
)

func TestValidateSnapshotID(t *testing.T) {
	var inputs = []struct {
		val   string
		fails bool
	}{
		{"__--aazz0099--__", false},
		{"az09-_~", true},
		{"az09-_A", true},
	}

	for _, i := range inputs {

		err := ValidateSnapshotID(i.val)

		if i.fails && err == nil {
			t.Fatalf("expected error, got %s", err)
		}
		if !i.fails && err != nil {
			t.Fatalf("expected <ni>, got %s", err)
		}
	}
}

func TestValidateCreateSnapshotRequest(t *testing.T) {
	var inputs = []struct {
		val   *apiv1.CreateSnapshotRequest
		fails bool
	}{
		{
			&apiv1.CreateSnapshotRequest{},
			false,
		},
		{
			&apiv1.CreateSnapshotRequest{SnapshotID: "foo"},
			false,
		},
		{
			&apiv1.CreateSnapshotRequest{SnapshotID: "~foo"},
			true,
		},
	}

	for _, i := range inputs {

		err := ValidateCreateSnapshotRequest(i.val)

		if i.fails && err == nil {
			t.Fatalf("expected error, got %s", err)
		}
		if !i.fails && err != nil {
			t.Fatalf("expected <ni>, got %s", err)
		}
	}
}

func TestValidateStartCVDRequest(t *testing.T) {
	var inputs = []struct {
		val   *apiv1.StartCVDRequest
		fails bool
	}{
		{
			&apiv1.StartCVDRequest{},
			false,
		},
		{
			&apiv1.StartCVDRequest{SnapshotID: "foo"},
			false,
		},
		{
			&apiv1.StartCVDRequest{SnapshotID: "~foo"},
			true,
		},
	}

	for _, i := range inputs {

		err := ValidateStartCVDRequest(i.val)

		if i.fails && err == nil {
			t.Fatalf("expected error, got %s", err)
		}
		if !i.fails && err != nil {
			t.Fatalf("expected <ni>, got %s", err)
		}
	}
}

func TestValidateFileName(t *testing.T) {
	var inputs = []struct {
		val   string
		fails bool
	}{
		{"tmp.foo", false},
		{"/tmp/foo", true},
		{".", true},
		{"..", true},
	}

	for _, i := range inputs {

		err := ValidateFileName(i.val)

		if i.fails && err == nil {
			t.Fatalf("expected error, got %s", err)
		}
		if !i.fails && err != nil {
			t.Fatalf("expected <nil>, got %s", err)
		}
	}
}
