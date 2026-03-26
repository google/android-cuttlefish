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
package e2etests

import (
	"bytes"
	"context"
	"encoding/xml"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"log"
	"os"
	"os/exec"
	"os/signal"
	"path"
	"path/filepath"
	"strings"
	"syscall"
	"testing"

	"github.com/bazelbuild/rules_go/go/runfiles"
)

// Checks if a file exists.
func FileExists(f string) bool {
	info, err := os.Stat(f)
	if errors.Is(err, fs.ErrNotExist) {
		return false
	}
	if info.IsDir() {
		return false
	}
	return true
}

// Checks if a directory exists.
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

// Common state while running an e2etest.
type TestContext struct {
	t *testing.T
	tempdir string

	// This is used instead of `T.Cleanup()` to support running commands during
	// cleanup. The context from `T.Context()` is cancelled before `T.Cleanup()`
	// registered cleanup functions are invoked.
	cleanups []func()
	teardownCalled bool

	context context.Context
	cancelled bool
}

func runCmdWithContextEnv(ctx context.Context, command []string, envvars map[string]string) (string, error) {
	cmd := exec.CommandContext(ctx, command[0], command[1:]...)

	cmdOutputBuf := bytes.Buffer{}
	cmdWriter := io.MultiWriter(&cmdOutputBuf, log.Writer())
	cmd.Stdout = cmdWriter
	cmd.Stderr = cmdWriter

	envvarPairs := []string{};
	for k, v := range envvars {
		envvarPairs = append(envvarPairs, fmt.Sprintf("%s=%s", k, v))
	}
	cmd.Env = os.Environ()
	cmd.Env = append(cmd.Env, envvarPairs...)

	log.Printf("Running `%s %s`\n", strings.Join(envvarPairs, " "), strings.Join(command, " "))
	err := cmd.Run()
	if err != nil {
		return "", fmt.Errorf("`%s` failed: %w", strings.Join(command, " "), err)
	}

	return cmdOutputBuf.String(), nil
}

// Runs the given command with the given set of envvars overrided.
func (tc *TestContext) RunCmdWithEnv(command []string, envvars map[string]string) (string, error) {
	return runCmdWithContextEnv(tc.context, command, envvars)
}

// Waits for a device to be available via adb.
func (tc *TestContext) RunAdbWaitForDevice() error {
	adbCommand := []string{
		"timeout",
		"--kill-after=30s",
		"29s",
		"adb",
		"wait-for-device",
	}
	if _, err := tc.RunCmd(adbCommand...); err != nil {
		return fmt.Errorf("timed out waiting for Cuttlefish device to connect to adb: %w", err)
	}
	return nil
}

// Runs the given command with the existing envvars.
func (tc *TestContext) RunCmd(args... string) (string, error) {
	command := []string{}
	command = append(command, args...)
	return tc.RunCmdWithEnv(command, map[string]string{})
}

// Common parameters passed to `cvd fetch`.
type FetchArgs struct {
    DefaultBuildBranch string
	DefaultBuildTarget string
	TestSuiteBuildBranch string
	TestSuiteBuildTarget string
}

// Common parameters passed to `cvd create`.
type CreateArgs struct {
	Args []string
}

// Common parameters to fetch and create a Cuttlefish device.
type FetchAndCreateArgs struct {
	Fetch FetchArgs
	Create CreateArgs
}

// Performs `cvd fetch <args>`.
func (tc *TestContext) CVDFetch(args FetchArgs) error {
	log.Printf("Fetching...")
	fetchCmd := []string{
		"cvd",
		"fetch",
		fmt.Sprintf("--default_build=%s/%s", args.DefaultBuildBranch, args.DefaultBuildTarget),
		fmt.Sprintf("--target_directory=%s", tc.tempdir),
	}
	if args.TestSuiteBuildBranch != "" && args.TestSuiteBuildTarget != "" {
		fetchCmd = append(fetchCmd, fmt.Sprintf("--test_suites_build=%s/%s", args.TestSuiteBuildBranch, args.TestSuiteBuildTarget))
	}

	credentialArg := os.Getenv("CREDENTIAL_SOURCE")
	if credentialArg != "" {
		fetchCmd = append(fetchCmd, fmt.Sprintf("--credential_source=%s", credentialArg))
	}
	if _, err := tc.RunCmd(fetchCmd...); err != nil {
		log.Printf("Failed to fetch: %w", err)
		return err
	}

	// Android CTS includes some files with a `kernel` suffix which confuses the
	// Cuttlefish launcher prior to
	// https://github.com/google/android-cuttlefish/commit/881728ed85329afaeb16e3b849d60c7a32fedcb7.
	log.Printf("Adjusting fetcher_config.json to work around old launcher limitation...")
	tc.RunCmd("sed", "-i", `s|_kernel\"|_kernel_zzz\"|g`, path.Join(tc.tempdir, "/fetcher_config.json"))

	log.Printf("Fetch completed!")

	return nil
}

// Performs `cvd create <args>`.
func (tc *TestContext) CVDCreate(args CreateArgs) error {
	tempdirEnv := map[string]string{
		"HOME": tc.tempdir,
	}

	createCmd := []string{"cvd", "create"};
	createCmd = append(createCmd, "--report_anonymous_usage_stats=y")
	createCmd = append(createCmd, "--undefok=report_anonymous_usage_stats")
	if len(args.Args) > 0 {
		createCmd = append(createCmd, args.Args...)
	}
	if _, err := tc.RunCmdWithEnv(createCmd, tempdirEnv); err != nil {
		log.Printf("Failed to create instance(s): %w", err)
		return err
	}

	tc.Cleanup(func() { tc.CVDStop() })
	return nil
}

// Performs `cvd stop`.
func (tc *TestContext) CVDStop() error {
	tempdirEnv := map[string]string{
		"HOME": tc.tempdir,
	}

	stopCmd := []string{"cvd", "stop"};
	if _, err := tc.RunCmdWithEnv(stopCmd, tempdirEnv); err != nil {
		log.Printf("Failed to stop instance(s): %w", err)
		return err
	}

	return nil
}

// Performs `HOME=<testdir> bin/launch_cvd <args>`.
func (tc *TestContext) LaunchCVD(args CreateArgs) error {
	tempdirEnv := map[string]string{
		"HOME": tc.tempdir,
	}

	createCmd := []string{"bin/launch_cvd", "--daemon"}
	createCmd = append(createCmd, "--report_anonymous_usage_stats=y")
	createCmd = append(createCmd, "--undefok=report_anonymous_usage_stats")
	if len(args.Args) > 0 {
		createCmd = append(createCmd, args.Args...)
	}
	if _, err := tc.RunCmdWithEnv(createCmd, tempdirEnv); err != nil {
		log.Printf("Failed to create instance(s): %w", err)
		return err
	}


	tc.Cleanup(func() { tc.StopCVD() })
	return nil
}

// Performs `HOME=<testdir> bin/stop_cvd`.
func (tc *TestContext) StopCVD() error {
	tempdirEnv := map[string]string{
		"HOME": tc.tempdir,
	}

	stopCmd := []string{"bin/stop_cvd"};
	if _, err := tc.RunCmdWithEnv(stopCmd, tempdirEnv); err != nil {
		log.Printf("Failed to stop instance(s): %w", err)
		return err
	}

	return nil
}

// Performs `cvd powerwash`.
func (tc *TestContext) CVDPowerwash() error {
	tempdirEnv := map[string]string{
		"HOME": tc.tempdir,
	}

	createCmd := []string{"cvd", "powerwash"};
	if _, err := tc.RunCmdWithEnv(createCmd, tempdirEnv); err != nil {
		log.Printf("Failed to powerwash instance(s): %w", err)
		return err
	}

	return nil
}

// Common parameters for `cvd load`.
type LoadArgs struct {
	LoadConfig string
}

// Performs `cvd load`.
func  (tc *TestContext) CVDLoad(load LoadArgs) error {
	configpath := path.Join(tc.tempdir, "cvd_load_config.json")

	log.Printf("Writing config to %s", configpath)

	err := os.WriteFile(configpath, []byte(load.LoadConfig), os.ModePerm)
	if err != nil {
		log.Printf("Failed to write load config to file: %w", err)
		return err
	}

	log.Printf("Creating instance(s) via `cvd load`...")
	loadCmd := []string{
		"cvd",
		"load",
		configpath,
	};
	credentialArg := os.Getenv("CREDENTIAL_SOURCE")
	if credentialArg != "" {
		loadCmd = append(loadCmd, fmt.Sprintf("--credential_source=%s", credentialArg))
	}
	if _, err := tc.RunCmd(loadCmd...); err != nil {
		log.Printf("Failed to perform `cvd load`: %w", err)
		return err
	}
	log.Printf("Created instance(s) via `cvd load`!")

	tc.Cleanup(func() { tc.CVDStop() })
	return nil
}

// Creates a standard environment for an e2etests.
func (tc *TestContext) SetUp(t *testing.T) {
	tc.t = t
	tc.teardownCalled = false
	cancellableContext, cancel := context.WithCancel(t.Context())
	tc.context = cancellableContext

	log.Printf("Initializing %s test...", tc.t.Name())

	log.Printf("Cleaning up any pre-existing instances...")
	if _, err := tc.RunCmd("cvd", "reset", "-y"); err != nil {
		log.Printf("Failed to cleanup any pre-existing instances: %w", err)
	}
	log.Printf("Finished cleaning up any pre-existing instances!")

	tc.tempdir = tc.t.TempDir()

	log.Printf("Chdir to %s", tc.tempdir)
	tc.t.Chdir(tc.tempdir)

	marker := os.Getenv("LOCAL_DEBIAN_SUBSTITUTION_MARKER_FILE")
	if marker != "" {
		marker, err := runfiles.Rlocation(marker)
		if err != nil {
			tc.t.Fatalf("failed to find substituion runfile from %s: %w", marker, err)
		}

		full, err := filepath.EvalSymlinks(marker)
		if err != nil {
			tc.t.Fatalf("failed to read substituion marker full path from %s: %w", marker, err)
		}

		tc.t.Setenv("LOCAL_DEBIAN_SUBSTITUTION_MARKER_FILE", full)
	}

	log.Printf("Initialized %s test!", tc.t.Name())

	sigChan := make(chan os.Signal, 10)
	signal.Notify(sigChan, os.Interrupt, syscall.SIGTERM)

	go func() {
		for sig := range sigChan {
			t.Errorf("Received signal '%s'", sig.String())
			tc.cancelled = true
			cancel()
		}
	}()

	if tc.cancelled {
		t.Fatalf("Previous test was cancelled")
	}

	tc.t.Cleanup(func() {
		if !tc.teardownCalled {
			tc.t.Fatalf("Test %s forgot to call TearDown().", tc.t.Name())
		}
	})
}

// Registers a callback to run during test teardown.
func (tc *TestContext) Cleanup(f func()) {
	tc.cleanups = append(tc.cleanups, f)
}

// Destroys a standard environment for an e2etests.
func (tc *TestContext) TearDown() {
	log.Printf("Cleaning up after test...")
	tc.teardownCalled = true

	log.Printf("Running registered cleanup callbacks...")
	for i := len(tc.cleanups) - 1; i >= 0; i-- {
		tc.cleanups[i]()
	}
	log.Printf("Finished running registered cleanup callbacks!")

	outdir := os.Getenv("TEST_UNDECLARED_OUTPUTS_DIR")
	if outdir != "" {
		testoutdir := path.Join(outdir, tc.t.Name())

		err := os.MkdirAll(testoutdir, os.ModePerm)
		if err == nil {
			log.Printf("Copying logs to test output directory...")

			patterns := [...]string{
				"cuttlefish_runtime/cuttlefish_config.json",
				"cuttlefish_runtime/logcat",
				"cuttlefish_runtime/*.log",
			}
			for _, pattern := range patterns {
				matches, err := filepath.Glob(path.Join(tc.tempdir, pattern))
				if err == nil {
					for _, file := range matches {
						_, err := tc.RunCmd("cp", "--dereference", file, testoutdir)
						if err != nil {
							log.Printf("failed to copy %s to %s: %w", file, testoutdir, err)
						}
					}
				} else {
					log.Printf("failed to glob files matching %s: %w", pattern, err)
				}
			}

			matches, err := filepath.Glob(path.Join(tc.tempdir, "cuttlefish/instances/*"))
			if err == nil {
				for _, instancedir := range matches {
					instance := filepath.Base(instancedir)

					outinstancedir := path.Join(testoutdir, fmt.Sprintf("instance_%s", instance))
					err := os.MkdirAll(outinstancedir, os.ModePerm)
					if err == nil {
						logdir := path.Join(instancedir, "logs")
						_, err := runCmdWithContextEnv(context.TODO(), []string{"cp", "-r", "--dereference", logdir, outinstancedir}, map[string]string{})
						if err != nil {
							log.Printf("failed to copy %s to %s: %w", logdir, outinstancedir, err)
						}
					}
				}
			}

			log.Printf("Finished copying logs to test output directory!")
		} else {
			log.Printf("failed to mkdir output directory %s: %w", outdir, err)
		}
	}

	runCmdWithContextEnv(context.TODO(), []string{"cvd", "reset", "-y"}, map[string]string{})

	log.Printf("Finished cleaning up after test!")
}

func xtsTradefedPath(args XtsArgs) string {
	return fmt.Sprintf("android-%s/tools/%s-tradefed", args.XtsType, args.XtsType)
}

func findLocalXTS(cuttlefishArgs FetchAndCreateArgs, xtsArgs XtsArgs) string {
	// TODO: explore adding a non-user-specific cache option to cvd.

	homedir, err := os.UserHomeDir()
    if err != nil {
		user := os.Getenv("USER")
		if user == "" {
			return ""
		}
		homedir = path.Join("/home", user)
    }

	possibleDir := path.Join(homedir, cuttlefishArgs.Fetch.TestSuiteBuildBranch, cuttlefishArgs.Fetch.TestSuiteBuildTarget)
	log.Printf("Checking %s", possibleDir)
	if !DirectoryExists(possibleDir) {
		return ""
	}

	possibleTradefed := path.Join(possibleDir, xtsTradefedPath(xtsArgs))
	if !FileExists(possibleTradefed) {
		return ""
	}

	return possibleDir
}

type xtsTest struct {
	Name string `xml:"name,attr"`
	Result string `xml:"result,attr"`
}

type xtsTestCase struct {
	Name string `xml:"name,attr"`
	Tests []xtsTest `xml:"Test"`
}

type xtsModule struct {
	Name string `xml:"name,attr"`
	TestCases []xtsTestCase `xml:"TestCase"`
}

type xtsSummary struct {
	Pass int `xml:"pass,attr"`
	Failed int `xml:"failed,attr"`
	ModulesDone int `xml:"modules_done,attr"`
	ModulesTotal int `xml:"modules_total,attr"`
}

type xtsResult struct {
    Summary xtsSummary `xml:"Summary"`
	Modules []xtsModule `xml:"Module"`
}

// Common parameters for running CTS/VTS/etc.
type XtsArgs struct {
	XtsArgs []string
	XtsType string
}

// Fetches a given Cuttlefish build, creates a Cufflefish instance, and runs CTS/VTS/etc against the device.
func RunXts(t *testing.T, cuttlefishArgs FetchAndCreateArgs, xtsArgs XtsArgs) {
	tc := TestContext{}
	tc.SetUp(t)
	defer tc.TearDown()

	localXtsDir := findLocalXTS(cuttlefishArgs, xtsArgs)
	if localXtsDir != "" {
		log.Printf("Re-using existing XTS at %s", localXtsDir)
		cuttlefishArgs.Fetch.TestSuiteBuildBranch = ""
		cuttlefishArgs.Fetch.TestSuiteBuildTarget = ""
	} else {
		log.Printf("Failed to find existing XTS, will fetch.")
	}

	if err := tc.CVDFetch(cuttlefishArgs.Fetch); err != nil {
		t.Fatal(err)
	}

	if err := tc.CVDCreate(cuttlefishArgs.Create); err != nil {
		t.Fatal(err)
	}

	if err := tc.RunAdbWaitForDevice(); err != nil {
		t.Fatalf("failed to wait for Cuttlefish device to connect to adb: %w", err)
	}

	xtsDir := "test_suites"
	if localXtsDir != "" {
		xtsDir = localXtsDir
	}

	log.Printf("Chdir to %s", xtsDir)
	tc.t.Chdir(xtsDir)

	log.Printf("Running XTS...")
	xtsCommand := []string{
		xtsTradefedPath(xtsArgs),
		"run",
		"commandAndExit",
		xtsArgs.XtsType,
		"--log-level-display=INFO",
	};
	xtsCommand = append(xtsCommand, xtsArgs.XtsArgs...)
	if _, err := tc.RunCmd(xtsCommand...); err != nil {
		t.Fatalf("failed to fully run XTS: %w", err)
	}
	log.Printf("Finished running XTS!")

	log.Printf("Parsing XTS results...")
	xtsResultsPath := path.Join(fmt.Sprintf("android-%s", xtsArgs.XtsType), "results", "latest", "test_result.xml")
	xtsResultsBytes, err := os.ReadFile(xtsResultsPath)
	if err != nil {
		t.Fatalf("failed to read XTS XML results from %s: %w", xtsResultsPath, err)
	}

	var xtsResult xtsResult
	if err := xml.Unmarshal([]byte(xtsResultsBytes), &xtsResult); err != nil {
		t.Fatalf("failed to parse XTS XML results from %s: %w", xtsResultsPath, err)
	}

	for _, xtsModule := range(xtsResult.Modules) {
		for _, xtsTestCase := range(xtsModule.TestCases) {
			for _, xtsTest := range(xtsTestCase.Tests) {
				testname := fmt.Sprintf("%s#%s", xtsTestCase.Name, xtsTest.Name)
				t.Run(testname, func(t *testing.T) {
					log.Printf("%s result: %s", testname, xtsTest.Result)
					if xtsTest.Result == "failed" {
						t.Error("XTS test failed, see logs")
					}
				})
			}
		}
	}
	log.Printf("Finished parsing XTS results!", err)
}
