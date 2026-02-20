package main

import (
	"fmt"
	"strings"
	"testing"

	"github.com/google/android-cuttlefish/e2etests/cvd/cvd_common"
)

func TestLaunchingWithAutoEnablesGfxstream(t *testing.T) {
	testArgs := cvd_common.TestArgs{
		Name: t.Name(),
		PostBootFunc: func(ctx cvd_common.TestContext) error {
			err := ctx.RunAdbWaitForDevice()
			if err != nil {
				return err
			}

			cmd := []string{
				"adb",
				"shell",
				"getprop",
				"ro.hardware.egl",
			}
			output, err := ctx.RunCmd(cmd...)
			if err != nil {
				return fmt.Errorf("Failed to get EGL sysprop: %w", err)
			}
			output = strings.TrimSpace(output)
			if output != "emulation" {
				t.Errorf(`"ro.hardware.egl" was "%s"; expected "emulation"`, output)
			}
			return nil
		},
	}
	createArgs := cvd_common.FetchAndCreateArgs{
		Fetch: cvd_common.FetchArgs{
			DefaultBuildBranch: "aosp-android-latest-release",
			DefaultBuildTarget: "aosp_cf_x86_64_only_phone-userdebug",
		},
		Create: cvd_common.CreateArgs{
			Runner: cvd_common.CvdCreate,
		},
	}
	err := cvd_common.RunTest(testArgs, createArgs)
	if err != nil {
		t.Error(err)
	}
}
