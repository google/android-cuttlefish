// Copyright (C) 2024 The Android Open Source Project
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

package orchestration

import (
	"os"
	"testing"

	"orchestration/e2etesting"

	hoapi "github.com/google/android-cuttlefish/frontend/src/liboperator/api/v1"
	orchclient "github.com/google/cloud-android-orchestration/pkg/client"
	"github.com/google/go-cmp/cmp"
)

func TestCreateSingleInstance(t *testing.T) {
	ctx, err := e2etesting.Setup(61000)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() {
		e2etesting.Cleanup(ctx)
	})
	buildID := os.Getenv("BUILD_ID")
	buildTarget := os.Getenv("BUILD_TARGET")
	srv := orchclient.NewHostOrchestratorService(ctx.ServiceURL)
	createReq := &hoapi.CreateCVDRequest{
		CVD: &hoapi.CVD{
			BuildSource: &hoapi.BuildSource{
				AndroidCIBuildSource: &hoapi.AndroidCIBuildSource{
					MainBuild: &hoapi.AndroidCIBuild{
						BuildID: buildID,
						Target:  buildTarget,
					},
				},
			},
		},
	}

	got, err := srv.CreateCVD(createReq, "")

	if err != nil {
		t.Fatal(err)
	}
	want := &hoapi.CreateCVDResponse{
		CVDs: []*hoapi.CVD{
			&hoapi.CVD{
				Group:          "cvd",
				Name:           "1",
				BuildSource:    &hoapi.BuildSource{},
				Status:         "Running",
				Displays:       []string{"720 x 1280 ( 320 )"},
				WebRTCDeviceID: "cvd-1",
				ADBSerial:      "0.0.0.0:6520",
			},
		},
	}
	if diff := cmp.Diff(want, got); diff != "" {
		t.Errorf("response mismatch (-want +got):\n%s", diff)
	}
}
