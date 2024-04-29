// Copyright 2023 Google LLC
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
	"context"
	"os/exec"
	"path"
	"strings"
	"testing"

	orchtesting "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/testing"
	apiv1 "github.com/google/android-cuttlefish/frontend/src/liboperator/api/v1"

	"github.com/google/go-cmp/cmp"
)

func TestListCVDsSucceeds(t *testing.T) {
	dir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, dir)
	output :=
		`{
                  "groups": [
                    {
                      "group_name": "foo",
                      "instances": [
                        {
                          "adb_serial": "0.0.0.0:6520",
                          "assembly_dir": "/var/lib/cuttlefish-common/runtimes/cuttlefish/assembly",
                          "displays": [
                            "720 x 1280 ( 320 )"
                          ],
                          "instance_dir": "/var/lib/cuttlefish-common/runtimes/cuttlefish/instances/cvd-1",
                          "instance_name": "1",
                          "status": "Running",
                          "web_access": "https://localhost:1443/devices/cvd-1/files/client.html",
                          "webrtc_device_id": "cvd-1",
                          "webrtc_port": "8443"
                        }
                      ]
                    },
                    {
                      "group_name": "bar",
                      "instances": [
                        {
                          "adb_serial": "0.0.0.0:6520",
                          "assembly_dir": "/var/lib/cuttlefish-common/runtimes/cuttlefish/assembly",
                          "displays": [
                            "720 x 1280 ( 320 )"
                          ],
                          "instance_dir": "/var/lib/cuttlefish-common/runtimes/cuttlefish/instances/cvd-1",
                          "instance_name": "1",
                          "status": "Running",
                          "web_access": "https://localhost:1443/devices/cvd-1/files/client.html",
                          "webrtc_device_id": "cvd-1",
                          "webrtc_port": "8443"
                        }
                      ]
                    }
                  ]
                }`
	execContext := func(ctx context.Context, name string, args ...string) *exec.Cmd {
		cmd := exec.Command("true")
		if path.Base(args[len(args)-1]) == "fleet" {
			// Prints to stderr as well to make sure only stdout is used.
			cmd = exec.Command("tee", "/dev/stderr")
			cmd.Stdin = strings.NewReader(strings.TrimSpace(output))
		}
		return cmd
	}
	paths := IMPaths{ArtifactsRootDir: dir + "/artifacts"}
	opts := ListCVDsActionOpts{
		Paths:       paths,
		ExecContext: execContext,
		CVDUser:     fakeCVDUser,
	}
	action := NewListCVDsAction(opts)

	res, _ := action.Run()

	want := &apiv1.ListCVDsResponse{CVDs: []*apiv1.CVD{
		{
			Group:          "foo",
			Name:           "1",
			BuildSource:    &apiv1.BuildSource{},
			Status:         "Running",
			Displays:       []string{"720 x 1280 ( 320 )"},
			WebRTCDeviceID: "cvd-1",
			ADBSerial:      "0.0.0.0:6520",
		},
	}}
	if diff := cmp.Diff(want, res); diff != "" {
		t.Errorf("response mismatch (-want +got):\n%s", diff)
	}
}
