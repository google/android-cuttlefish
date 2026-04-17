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
	"fmt"
	"testing"

	"github.com/google/android-cuttlefish/e2etests/cvd/common"
	"github.com/google/android-cuttlefish/e2etests/cvd/cvd_powerwash_tests/common"
)

func TestCvdPowerwash(t *testing.T) {
	testcases := []struct {
		shortName string
		branch string
		target string
		createArgs []string
	}{
		{
			shortName: "Aosp",
			branch: "aosp-android-latest-release",
			target: "aosp_cf_x86_64_only_phone-userdebug",
			createArgs: []string{},
		},
		{
			shortName: "AospGfxstreamSwangle",
			branch: "aosp-android-latest-release",
			target: "aosp_cf_x86_64_only_phone-userdebug",
			createArgs: []string{
				"--gpu_mode=gfxstream_guest_angle_host_swiftshader",
			},
		},
	}
	c := e2etests.TestContext{}
	for _, tc := range testcases {
		t.Run(fmt.Sprintf("BUILD=%s", tc.shortName), func(t *testing.T) {
			c.SetUp(t)
			defer c.TearDown()

			if err := c.CVDFetch(e2etests.FetchArgs{
				DefaultBuildBranch: tc.branch,
				DefaultBuildTarget: tc.target,
			}); err != nil {
				t.Fatal(err)
			}

			if err := c.CVDCreate(e2etests.CreateArgs{Args: tc.createArgs}); err != nil {
				t.Fatal(err)
			}

			if err := powerwash_common.VerifyPowerwash(&c); err != nil {
				t.Fatal(err)
			}
		})
	}
}
