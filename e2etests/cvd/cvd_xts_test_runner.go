package main

import (
	"flag"
	"fmt"
	"log"
	"os"

	"github.com/google/android-cuttlefish/e2etests/cvd/cvd_common"
)

type XtsArgs struct {
	XtsArgs string
	XtsType string
	XtsXmlConverterBinary string
}

func (c XtsArgs) DeclareFlags() {
	flag.StringVar(&c.XtsArgs, "xts_args", "", "Additional arguments passed to `cvd create <args>`.")
	flag.StringVar(&c.XtsType, "xts_type", "", "The target of the default build to fetch.")
}

func RunXts(ctx cvd_common.TestContext, args XtsArgs) error {
	adbCommand := []string{
		"timeout",
		"--kill-after=30s",
		"29s",
		"adb",
		"wait-for-device",
	}
	if _, err := ctx.RunCmd(adbCommand...); err != nil {
		log.Printf("Timed out waiting for Cuttlefish device to connect to adb.")
		return err
	}

	olddir, err := os.Getwd()
	if err != nil {
		log.Printf("Failed to get cwd before XTS: %w", err)
		return err
	}
	err = os.Chdir("test_suites")
	if err != nil {
		log.Printf("Failed to chdir to XTS directory: %w", err)
		return err
	}

	defer func(){
		err := os.Chdir(olddir)
		if err != nil {
			log.Printf("Failed to chdir back to original: %w", err)
		}
	}()

	xtsCommand := []string{
		fmt.Sprintf("android-%s/tools/%s-tradefed", args.XtsType, args.XtsType),
		"run",
		"commandAndExit",
		args.XtsType,
		"--log-level-display=INFO",
		args.XtsArgs,
	};
	if _, err := ctx.RunCmd(xtsCommand...); err != nil {
		log.Printf("XTS command did not complete successfully.")
		return err
	}

	return nil
}

func main() {
	createArgs := cvd_common.FetchAndCreateArgs{}
	createArgs.DeclareFlags()

	xtsArgs := XtsArgs{}
	xtsArgs.DeclareFlags()

	flag.Parse()

	testArgs := cvd_common.TestArgs{
		Name: "xts",
		PostBootFunc: func(ctx cvd_common.TestContext) error {
			return RunXts(ctx, xtsArgs)
		},
	}
	if err := cvd_common.RunTest(testArgs, createArgs); err != nil {
		log.Printf("Failed to run to completion: %w", err)
		os.Exit(1)
	}

	os.Exit(0)
}
