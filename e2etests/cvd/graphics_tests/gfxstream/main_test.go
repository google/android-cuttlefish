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

	"github.com/google/android-cuttlefish/e2etests/cvd/common"
)

func TestGfxstream(t *testing.T) {
	e2etests.RunXts(t,
		e2etests.FetchAndCreateArgs{
			Fetch: e2etests.FetchArgs{
				DefaultBuildBranch: "aosp-android-latest-release",
				DefaultBuildTarget: "aosp_cf_x86_64_only_phone-userdebug",
				TestSuiteBuildBranch: "aosp-android15-tests-release",
				TestSuiteBuildTarget: "test_suites_x86_64",
			},
			Create: e2etests.CreateArgs{
				Args: []string{
					"--gpu_mode=gfxstream",
				},
			},
		},
		e2etests.XtsArgs{
			XtsType: "cts",
			XtsArgs: []string{
				"--include-filter=CtsDeqpTestCases",
				"--module-arg=CtsDeqpTestCases:include-filter:dEQP-VK.api.smoke*",
			},
		})
}
