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
	"fmt"
	"regexp"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	"github.com/google/android-cuttlefish/frontend/src/liboperator/operator"
)

var snapshotIDRegex = regexp.MustCompile(`^([a-z0-9\-_]+)$`)

func ValidateSnapshotID(v string) error {
	if !snapshotIDRegex.MatchString(v) {
		return operator.NewBadRequestError(
			"invalid snapshot id value", fmt.Errorf("%s does not match %s", v, snapshotIDRegex))
	}
	return nil
}

func ValidateCreateSnapshotRequest(r *apiv1.CreateSnapshotRequest) error {
	if r.SnapshotID != "" {
		if err := ValidateSnapshotID(r.SnapshotID); err != nil {
			return operator.NewBadRequestError("invalid request", err)
		}
	}
	return nil
}
