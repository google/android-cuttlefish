package main

import (
	"errors"
	"fmt"
	"io/fs"
	"log"
	"os"
	"path"
	"path/filepath"
	"regexp"
	"testing"

	"github.com/google/android-cuttlefish/e2etests/cvd/cvd_common"
)

func DirectoryExists(d string) bool {
	info, err := os.Stat(d)
	if errors.Is(err, fs.ErrNotExist) {
		return false
	}
	if !info.IsDir() {
		return false
	}
	return true
}

func AnyFileExists(pattern string) bool {
	matches, err := filepath.Glob(pattern)
	if err != nil {
		return false
	}
	return len(matches) > 0
}

func TestListEnvServices(t *testing.T) {
	testArgs := cvd_common.TestArgs{
		Name: t.Name(),
		PostBootFunc: func(ctx cvd_common.TestContext) error {
			cmd := []string{
				"cvd",
				"env",
				"ls",
			}
			_, err := ctx.RunCmd(cmd...)
			return err
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

func TestTakeBugreport(t *testing.T) {
	testArgs := cvd_common.TestArgs{
		Name: t.Name(),
		PostBootFunc: func(ctx cvd_common.TestContext) error {
			cmd := []string{
				"cvd",
				"host_bugreport",
			}
			_, err := ctx.RunCmd(cmd...)
			return err
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

func TestAddDisplay(t *testing.T) {
	testArgs := cvd_common.TestArgs{
		Name: t.Name(),
		PostBootFunc: func(ctx cvd_common.TestContext) error {
			cmd := []string{
				"cvd",
				"display",
				"add",
				"--display=width=500,height=500",
			}
			_, err := ctx.RunCmd(cmd...)
			return err
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

func TestListDisplays(t *testing.T) {
	testArgs := cvd_common.TestArgs{
		Name: t.Name(),
		PostBootFunc: func(ctx cvd_common.TestContext) error {
			cmd := []string{
				"cvd",
				"display",
				"list",
			}
			_, err := ctx.RunCmd(cmd...)
			return err
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

func TestVerifyMetricsTransmission(t *testing.T) {
	var metricsdir string

	testArgs := cvd_common.TestArgs{
		Name: t.Name(),
		PostBootFunc: func(ctx cvd_common.TestContext) error {
			log.Printf("Checking for metrics files...")

			fleetCmd := []string{
				"cvd",
				"fleet",
			}
			output, err := ctx.RunCmd(fleetCmd...)
			if err != nil {
				log.Printf("Failed to run `cvd fleet`")
				return err
			}

			re := regexp.MustCompile(`"metrics_dir" : "(.*)",`)
			matches := re.FindStringSubmatch(output)
			if len(matches) != 2 {
				return fmt.Errorf("Failed to find metrics directory.")
			}

			metricsdir = matches[1]
			if !DirectoryExists(metricsdir) {
				return fmt.Errorf("Directory %s does not exist.", metricsdir)
			}

			patterns := []string{
				"device_instantiation*.txtpb",
				"device_boot_start*.txtpb",
				"device_boot_complete*.txtpb",
			}
			for _, p := range patterns {
				if !AnyFileExists(path.Join(metricsdir, p)) {
					return fmt.Errorf("Failed to find a `%s` file.", p)
				}
			}

			return nil
		},
		PostStopFunc: func(ctx cvd_common.TestContext) error {
			if !AnyFileExists(path.Join(metricsdir, "device_stop*.txtpb")) {
				return fmt.Errorf("Failed to find `device_stop*.txtpb` file.")
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

func TestFetchAndLaunchGitMainX64Phone(t *testing.T) {
	testArgs := cvd_common.TestArgs{
		Name: t.Name(),
	}
	createArgs := cvd_common.FetchAndCreateArgs{
		Fetch: cvd_common.FetchArgs{
			DefaultBuildBranch: "git_main",
			DefaultBuildTarget: "aosp_cf_x86_64_only_phone-trunk_staging-userdebug",
		},
		Create: cvd_common.CreateArgs{
			Runner: cvd_common.LaunchCvd,
		},
	}
	err := cvd_common.RunTest(testArgs, createArgs)
	if err != nil {
		t.Error(err)
	}
}

func TestFetchAndLaunchAospMainX86Phone(t *testing.T) {
	testArgs := cvd_common.TestArgs{
		Name: t.Name(),
	}
	createArgs := cvd_common.FetchAndCreateArgs{
		Fetch: cvd_common.FetchArgs{
			DefaultBuildBranch: "aosp-android-latest-release",
			DefaultBuildTarget: "aosp_cf_x86_64_only_phone-userdebug",
		},
		Create: cvd_common.CreateArgs{
			Runner: cvd_common.LaunchCvd,
		},
	}
	err := cvd_common.RunTest(testArgs, createArgs)
	if err != nil {
		t.Error(err)
	}
}

func TestFetchAndLaunchAndroid14PhoneGsi(t *testing.T) {
	testArgs := cvd_common.TestArgs{
		Name: t.Name(),
	}
	createArgs := cvd_common.FetchAndCreateArgs{
		Fetch: cvd_common.FetchArgs{
			DefaultBuildBranch: "aosp-android14-gsi",
			DefaultBuildTarget: "aosp_cf_x86_64_phone-userdebug",
		},
		Create: cvd_common.CreateArgs{
			Runner: cvd_common.LaunchCvd,
		},
	}
	err := cvd_common.RunTest(testArgs, createArgs)
	if err != nil {
		t.Error(err)
	}
}

func TestFetchAndLaunchAndroid13PhoneGsi(t *testing.T) {
	testArgs := cvd_common.TestArgs{
		Name: t.Name(),
	}
	createArgs := cvd_common.FetchAndCreateArgs{
		Fetch: cvd_common.FetchArgs{
			DefaultBuildBranch: "aosp-android13-gsi",
			DefaultBuildTarget: "aosp_cf_x86_64_phone-userdebug",
		},
		Create: cvd_common.CreateArgs{
			Runner: cvd_common.LaunchCvd,
		},
	}
	err := cvd_common.RunTest(testArgs, createArgs)
	if err != nil {
		t.Error(err)
	}
}

func TestFetchAndLaunchAndroid12PhoneGsi(t *testing.T) {
	testArgs := cvd_common.TestArgs{
		Name: t.Name(),
	}
	createArgs := cvd_common.FetchAndCreateArgs{
		Fetch: cvd_common.FetchArgs{
			DefaultBuildBranch: "aosp-android12-gsi",
			DefaultBuildTarget: "aosp_cf_x86_64_phone-userdebug",
		},
		Create: cvd_common.CreateArgs{
			Runner: cvd_common.LaunchCvd,
		},
	}
	err := cvd_common.RunTest(testArgs, createArgs)
	if err != nil {
		t.Error(err)
	}
}

func TestLoadGitMainX64Phone(t *testing.T) {
	testArgs := cvd_common.TestArgs{
		Name: t.Name(),
	}
	loadArgs := cvd_common.LoadArgs{
		LoadConfig: `
{
  "instances": [
    {
      "name": "ins-1",
      "disk": {
        "default_build": "@ab\/git_main\/aosp_cf_x86_64_only_phone-trunk_staging-userdebug"
      },
      "vm": {
        "cpus": 4,
        "memory_mb": 4096,
        "setupwizard_mode": "REQUIRED"
      },
      "graphics": {
        "displays": [
          {
            "width": 720,
            "height": 1280,
            "dpi": 140,
            "refresh_rate_hertz": 60
          }
        ],
        "record_screen": false
      }
    }
  ],
  "netsim_bt": false,
  "metrics": {
    "enable": true
  },
  "common": {
    "host_package": "@ab\/git_main\/aosp_cf_x86_64_only_phone-trunk_staging-userdebug"
  }
}`,
	}
	err := cvd_common.RunTest(testArgs, loadArgs)
	if err != nil {
		t.Error(err)
	}
}

func TestLoadAospMainX64Phone(t *testing.T) {
	testArgs := cvd_common.TestArgs{
		Name: t.Name(),
	}
	loadArgs := cvd_common.LoadArgs{
		LoadConfig: `
{
  "instances": [
    {
      "name": "ins-1",
      "disk": {
        "default_build": "@ab\/aosp-android-latest-release\/aosp_cf_x86_64_only_phone-userdebug",
        "super": {
          "system": "@ab\/aosp-android-latest-release\/aosp_cf_x86_64_only_phone-userdebug"
        }
      },
      "boot": {
        "kernel": {
          "build": "@ab\/aosp_kernel-common-android16-6.12\/kernel_virt_x86_64"
        },
        "build": "@ab\/aosp-android-latest-release\/aosp_cf_x86_64_only_phone-userdebug"
      },
      "vm": {
        "cpus": 4,
        "memory_mb": 4096,
        "setupwizard_mode": "REQUIRED"
      },
      "graphics": {
        "displays": [
          {
            "width": 720,
            "height": 1280,
            "dpi": 140,
            "refresh_rate_hertz": 60
          }
        ],
        "record_screen": false
      }
    }
  ],
  "netsim_bt": false,
  "metrics": {
    "enable": true
  },
  "common": {
    "host_package": "@ab\/aosp-android-latest-release\/aosp_cf_x86_64_only_phone-userdebug"
  }
}`,
	}
	err := cvd_common.RunTest(testArgs, loadArgs)
	if err != nil {
		t.Error(err)
	}
}

func TestLoadAospMainX64PhoneX2(t *testing.T) {
	testArgs := cvd_common.TestArgs{
		Name: t.Name(),
	}
	loadArgs := cvd_common.LoadArgs{
		LoadConfig: `
{
  "instances": [
    {
      "name": "ins-1",
      "disk": {
        "default_build": "@ab\/aosp-android-latest-release\/aosp_cf_x86_64_only_phone-userdebug"
      }
    },
    {
      "name": "ins-2",
      "disk": {
        "default_build": "@ab\/aosp-android-latest-release\/aosp_cf_x86_64_only_phone-userdebug"
      }
    }
  ],
  "metrics": {
    "enable": true
  },
  "common": {
    "host_package": "@ab\/aosp-android-latest-release\/aosp_cf_x86_64_only_phone-userdebug"
  }
}`,
	}
	err := cvd_common.RunTest(testArgs, loadArgs)
	if err != nil {
		t.Error(err)
	}
}
