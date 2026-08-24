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
	"context"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/google/android-cuttlefish/e2etests/cvd/common"
)

func TestV4l2StreamProxyCompliance(t *testing.T) {
	testcases := []struct {
		branch string
		target string
	}{
		{
			branch: "git_main",
			target: "aosp_cf_x86_64_only_phone-trunk_staging-userdebug",
		},
		{
			branch: "git_main-throttled-nightly",
			target: "aosp_cf_x86_64_auto-trunk_staging-userdebug",
		},
	}

	c := e2etests.TestContext{}
	for _, tc := range testcases {
		t.Run(fmt.Sprintf("BUILD-%s/%s", tc.branch, tc.target), func(t *testing.T) {
			c.SetUp(t)
			defer c.TearDown()

			if _, err := c.CVDFetch(e2etests.FetchArgs{
				DefaultBuildBranch: tc.branch,
				DefaultBuildTarget: tc.target,
			}); err != nil {
				t.Fatal(err)
			}

			fifoPath := filepath.Join(t.TempDir(), "v4l2_fifo")
			if _, err := c.RunCmd("mkfifo", fifoPath); err != nil {
				t.Fatalf("failed to create fifo %q: %v", fifoPath, err)
			}

			ctx, cancel := context.WithCancel(context.Background())
			defer cancel()

			go func() {
				for {
					f, err := os.OpenFile(fifoPath, os.O_WRONLY, 0)
					if err != nil {
						t.Logf("failed to open fifo for writing: %v", err)
						return
					}
					defer f.Close()

					frameSize := int(640 * 480 * 1.5)
					buf := make([]byte, frameSize)

					ticker := time.NewTicker(33 * time.Millisecond) // ~30fps
					defer ticker.Stop()
					
				workLoop:
					for {
						select {
						case <-ctx.Done():
							return
						case <-ticker.C:
							_, err := f.Write(buf)
							if err != nil {
								t.Logf("failed to write to fifo: %v", err)
								break workLoop
							}
						}
					}
				}
			}()

			mediaArg := fmt.Sprintf("--media=v4l2_stream_proxy:input_path=%s:input_width=640:input_height=480:input_fps=30", fifoPath)

			// When running with podcvd, we need to mount the fifo within the container as well, so the fifoPath is provided as an argument.
			// When not running with podcvd, it seems to be ignored (or at least not cause errors).
			if _, err := c.CVDCreate(e2etests.CreateArgs{
				Args: []string{mediaArg, fifoPath},
			}); err != nil {
				t.Fatal(err)
			}

			if err := c.RunAdbWaitForDevice(); err != nil {
				t.Fatalf("failed to wait for Cuttlefish device to connect to adb: %w", err)
			}

			// Find video node dynamically
			videoNode := ""
			for i := 0; i < 10; i++ {
				node := fmt.Sprintf("/dev/video%d", i)
				out, err := c.RunCmd("adb", "shell", "su", "0", "v4l2-ctl", "-d", node, "--info")
				if err == nil && strings.Contains(out.Stdout, "v4l2_stream_proxy") {
					videoNode = node
					break
				}
			}
			if videoNode == "" {
				t.Fatal("v4l2_stream_proxy device not found in guest")
			}
			t.Logf("Found v4l2_stream_proxy device at %s", videoNode)

			if _, err := c.RunCmd("adb", "shell", "su", "0", "v4l2-ctl", "--list-devices"); err != nil {
				t.Fatalf("v4l2-ctl --list-devices failed: %w", err)
			}

			if _, err := c.RunCmd("adb", "shell", "su", "0", "v4l2-compliance", "-d", videoNode, "-s"); err != nil {
				t.Fatalf("v4l2-compliance failed: %w", err)
			}
		})
	}
}
