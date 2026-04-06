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

// Package provides common functionality for launching and interacting
// with Cuttlefish instances for tests.

package powerwash_common

import (
	"fmt"

	"github.com/google/android-cuttlefish/e2etests/cvd/common"
)

func VerifyPowerwash(c *e2etests.TestContext) error {
	if err := c.RunAdbWaitForDevice(); err != nil {
		return fmt.Errorf("failed to wait for Cuttlefish device to connect to adb: %w", err)
	}

	const tmpFile = "/data/local/tmp/foo"
	if _, err := c.RunCmd("adb", "shell", "touch", tmpFile); err != nil {
		return fmt.Errorf("failed to create %s: %w", tmpFile, err)
	}

	if _, err := c.RunCmd("adb", "shell", "stat", tmpFile); err != nil {
		return fmt.Errorf("failed to verify %s created: %w", tmpFile, err)
	}

	if err := c.CVDPowerwash(); err != nil {
		return fmt.Errorf("failed to run powerwash command: %w", err)
	}

	if _, err := c.RunCmd("adb", "shell", "stat", tmpFile); err == nil {
		return fmt.Errorf("failed to powerwash, %s still exists", tmpFile)
	}

	return nil
}
