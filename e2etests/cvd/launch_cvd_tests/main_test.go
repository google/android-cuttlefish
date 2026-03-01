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

func TestLaunchCvd(t *testing.T) {
	testcases := []struct {
		name string
		branch string
		target string
	}{
		{
			name: "GitMainPhone",
			branch: "git_main",
			target: "aosp_cf_x86_64_only_phone-trunk_staging-userdebug",
		},
		{
			name: "AospMainPhone",
			branch: "aosp-android-latest-release",
			target: "aosp_cf_x86_64_only_phone-userdebug",
		},
		{
			name: "Aosp14GsiPhone",
			branch: "aosp-android14-gsi",
			target: "aosp_cf_x86_64_phone-userdebug",
		},
		{
			name: "Aosp13GsiPhone",
			branch: "aosp-android13-gsi",
			target: "aosp_cf_x86_64_phone-userdebug",
		},
		{
			name: "Aosp12GsiPhone",
			branch: "aosp-android12-gsi",
			target: "aosp_cf_x86_64_phone-userdebug",
		},
	}
	for _, tc := range testcases {
		t.Run(tc.name, func(t *testing.T) {
			c := e2etests.TestContext{}
			c.SetUp(t)

			if err := c.CVDFetch(e2etests.FetchArgs{
				DefaultBuildBranch: tc.branch,
				DefaultBuildTarget: tc.target,
			}); err != nil {
				t.Fatal(err)
			}

			if err := c.LaunchCVD(e2etests.CreateArgs{}); err != nil {
				t.Fatal(err)
			}

			c.TearDown()
		})
	}
}
