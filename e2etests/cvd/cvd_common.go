package cvd_common

import (
	"bytes"
	"context"
	"flag"
	"fmt"
	"io"
	"log"
	"os"
	"os/exec"
	"path"
	"path/filepath"
	"strings"
)

type TestContext struct {
	LogBuf bytes.Buffer
	Tempdir string
}

func (t TestContext) RunCmdWithEnv(command []string, envvars map[string]string) (string, error) {
	cmd := exec.CommandContext(context.TODO(), command[0], command[1:]...)

	cmdOutputBuf := bytes.Buffer{}

	logWriter := io.MultiWriter(&t.LogBuf, log.Writer())
	cmdWriter := io.MultiWriter(&cmdOutputBuf, logWriter)
	cmd.Stdout = cmdWriter
	cmd.Stderr = cmdWriter

	envvarPairs := []string{};
	for k, v := range envvars {
		envvarPairs = append(envvarPairs, fmt.Sprintf("%s=%s", k, v))
	}
	cmd.Env = os.Environ()
    cmd.Env = append(cmd.Env, envvarPairs...)

	fmt.Fprintf(logWriter, "Running `%s %s`\n", strings.Join(envvarPairs, " "), strings.Join(command, " "))
	err := cmd.Run()
	fmt.Fprint(logWriter, "\n")

	if err != nil {
		return "", fmt.Errorf("`%s` failed.", strings.Join(command, " "))
	}

	return string(cmdOutputBuf.Bytes()), nil
}

func (ctx TestContext) RunAdbWaitForDevice() error {
	adbCommand := []string{
		"timeout",
		"--kill-after=30s",
		"29s",
		"adb",
		"wait-for-device",
	}
	if _, err := ctx.RunCmd(adbCommand...); err != nil {
		return fmt.Errorf("Timed out waiting for Cuttlefish device to connect to adb.")
	}
	return nil
}

func (t TestContext) RunCmd(args... string) (string, error) {
	command := []string{}
	command = append(command, args...)
	return t.RunCmdWithEnv(command, map[string]string{})
}

type TestArgs struct {
	Name string
	// If provided, test callback to run after instances have booted.
	PostBootFunc func(ctx TestContext) error
	// If provided, test callback to run after instances have been stopped.
	PostStopFunc func(ctx TestContext) error
}

type FetchArgs struct {
    DefaultBuildBranch string
	DefaultBuildTarget string
	TestSuiteBuildBranch string
	TestSuiteBuildTarget string
}

func (c *FetchArgs) DeclareFlags() {
	flag.StringVar(&c.DefaultBuildBranch, "default_build_branch", "", "The branch of the default build to fetch.")
	flag.StringVar(&c.DefaultBuildTarget, "default_build_target", "", "The target of the default build to fetch.")
	flag.StringVar(&c.TestSuiteBuildBranch, "test_suite_build_branch", "", "The branch of the test suite build to fetch.")
	flag.StringVar(&c.TestSuiteBuildTarget, "test_suite_build_target", "", "The target of the test suite build to fetch.")
}

type CreateRunner int

const (
	CvdCreate CreateRunner = iota
	LaunchCvd
)

type CreateArgs struct {
	Runner CreateRunner
	Args []string
}

func (args *CreateArgs) DeclareFlags() {
	//flag.StringVar(&args.Args, "create_args", "", "The additional args passed to `cvd create`.")
}

type InstanceHandler interface {
	CreateInstances(ctx TestContext) error
	StopInstances(ctx TestContext) error
}

type FetchAndCreateArgs struct {
	Fetch FetchArgs
	Create CreateArgs
}

func (args *FetchAndCreateArgs) DeclareFlags() {
	args.Fetch.DeclareFlags()
	args.Create.DeclareFlags()
}

func (args FetchAndCreateArgs) CreateInstances(ctx TestContext) error {
	log.Printf("Fetching...")
	fetchCmd := []string{
		"cvd",
		"fetch",
		fmt.Sprintf("--default_build=%s/%s", args.Fetch.DefaultBuildBranch, args.Fetch.DefaultBuildTarget),
		fmt.Sprintf("--target_directory=%s", ctx.Tempdir),
	}
	if args.Fetch.TestSuiteBuildBranch != "" && args.Fetch.TestSuiteBuildTarget != "" {
		fetchCmd = append(fetchCmd, fmt.Sprintf("--test_suites_build=%s/%s", args.Fetch.TestSuiteBuildBranch, args.Fetch.TestSuiteBuildTarget))
	}

	credentialArg := os.Getenv("CREDENTIAL_SOURCE")
	if credentialArg != "" {
		fetchCmd = append(fetchCmd, fmt.Sprintf("--credential_source=%s", credentialArg))
	}
	if _, err := ctx.RunCmd(fetchCmd...); err != nil {
		log.Printf("Failed to fetch: %w", err)
		return err
	}
	log.Printf("Fetch completed!")

	// Android CTS includes some files with a `kernel` suffix which confuses the
	// Cuttlefish launcher prior to
	// https://github.com/google/android-cuttlefish/commit/881728ed85329afaeb16e3b849d60c7a32fedcb7.
	fixupCmd := []string{
		"sed",
		"-i",
		`s|_kernel\"|_kernel_zzz\"|g`,
		path.Join(ctx.Tempdir, "/fetcher_config.json"),
	}
	ctx.RunCmd(fixupCmd...)

	tempdirEnv := map[string]string{
		"HOME": ctx.Tempdir,
	}

	createCmd := []string{};
	switch args.Create.Runner {
	case CvdCreate:
		createCmd = append(createCmd, "cvd", "create")
	case LaunchCvd:
		createCmd = append(createCmd, "bin/launch_cvd")
		createCmd = append(createCmd, "--daemon")
	default:
		log.Fatal("Unknown runner")
	}
	createCmd = append(createCmd, "--report_anonymous_usage_stats=y")
	createCmd = append(createCmd, "--undefok=report_anonymous_usage_stats")
	if len(args.Create.Args) > 0 {
		createCmd = append(createCmd, args.Create.Args...)
	}
	if _, err := ctx.RunCmdWithEnv(createCmd, tempdirEnv); err != nil {
		log.Printf("Failed to create instance(s): %w", err)
		return err
	}

	return nil
}

func (args FetchAndCreateArgs) StopInstances(ctx TestContext) error {
	if args.Create.Runner == LaunchCvd {
		tempdirEnv := map[string]string{
			"HOME": ctx.Tempdir,
		}

		stopCmd := []string{"bin/stop_cvd"}
		if _, err := ctx.RunCmdWithEnv(stopCmd, tempdirEnv); err != nil {
			log.Printf("Failed to stop instance: %w", err)
			return err
		}
	}
	return nil
}

type LoadArgs struct {
	LoadConfig string
}

func (load LoadArgs) CreateInstances(ctx TestContext) error {
	configpath := path.Join(ctx.Tempdir, "cvd_load_config.json")

	err := os.WriteFile(configpath, []byte(load.LoadConfig), os.ModePerm)
	if err != nil {
		log.Printf("Failed to write load config to file: %w", err)
		return err
	}

	log.Printf("Creating instance(s) via `cvd load`...")
	loadCmd := []string{
		"cvd",
		"load",
		fmt.Sprintf("--config_file=%s", configpath),
	};
	credentialArg := os.Getenv("CREDENTIAL_SOURCE")
	if credentialArg != "" {
		loadCmd = append(loadCmd, fmt.Sprintf("--credential_source=%s", credentialArg))
	}
	if _, err := ctx.RunCmd(loadCmd...); err != nil {
		log.Printf("Failed to perform `cvd load`: %w", err)
		return err
	}
	log.Printf("Created instance(s) via `cvd load`!")

	return nil
}

func (load LoadArgs) StopInstances(ctx TestContext) error {
	log.Printf("Stopping instance(s) via `cvd stop`...")
	stopCmd := []string{
		"cvd",
		"stop",
	}
	if _, err := ctx.RunCmd(stopCmd...); err != nil {
		log.Printf("Failed to perform `cvd stop`: %w", err)
		return err
	}
	log.Printf("Stopped instance(s) via `cvd stop`!")

	return nil
}

func RunTest(test TestArgs, handler InstanceHandler) error {
	log.Printf("Running %s test...", test.Name)

	olddir, err := os.Getwd()
	if err != nil {
		log.Printf("Failed to get cwd: %w", err)
		return err
	}

	tempdir, err := os.MkdirTemp("", "cvd_test")
	if err != nil {
		log.Printf("Failed to create temporary directory for testing: %w", err)
		return err
	}

	if err := os.Chdir(tempdir); err != nil {
		log.Printf("Failed to chdir to %s", tempdir)
		return err
	}

	ctx := TestContext{
		LogBuf: bytes.Buffer{},
		Tempdir: tempdir,
	}

	defer func() {
		log.Printf("Cleaning up after test...")

		outdir := os.Getenv("TEST_UNDECLARED_OUTPUTS_DIR")
		if outdir != "" {
			outdir = path.Join(outdir, test.Name)

			err := os.MkdirAll(outdir, os.ModePerm)
			if err == nil {
				log.Printf("Copying logs to test output directory...")

				err := os.WriteFile(path.Join(outdir, "cvd_test_logs.txt"), ctx.LogBuf.Bytes(), os.ModePerm)
				if err != nil {
					log.Printf("Failed to write logs: %w", err)
				}

				patterns := [...]string{
					"cuttlefish_runtime/cuttlefish_config.json",
					"cuttlefish_runtime/logcat",
					"cuttlefish_runtime/*.log",
				}
				for _, pattern := range patterns {
					matches, err := filepath.Glob(path.Join(tempdir, pattern))
					if err == nil {
						for _, file := range matches {
							ctx.RunCmd("cp", "--dereference", file, outdir)
						}
					}
				}

				matches, err := filepath.Glob(path.Join(tempdir, "cuttlefish/instances/*"))
				if err == nil {
					for _, instancedir := range matches {
						instance := filepath.Base(instancedir)

						outinstancedir := path.Join(outdir, fmt.Sprintf("instance_%s", instance))
						err := os.MkdirAll(outinstancedir, os.ModePerm)
						if err == nil {
							ctx.RunCmd("cp", "-r", "--dereference", path.Join(instancedir, "logs"), outinstancedir)
						}
					}
				}

				log.Printf("Finished copying logs to test output directory!")
			}
		}

		ctx.RunCmd("cvd", "reset", "-y")

		os.Chdir(olddir)
		os.RemoveAll(tempdir)

		log.Printf("Finished cleaning up after test!")
	}()

	log.Printf("Cleaning up any pre-existing instances...")
	if _, err := ctx.RunCmd("cvd", "reset", "-y"); err != nil {
		log.Printf("Failed to cleanup any pre-existing instances: %w", err)
	}
	log.Printf("Finished cleaning up any pre-existing instances!")

	log.Printf("Creating Cuttlefish instance(s)...")
	err = handler.CreateInstances(ctx)
	if err != nil {
		log.Printf("Failed to create instance(s): %w", err)
		return err
	}
	log.Printf("Cuttlefish instance(s) created!")

	if test.PostBootFunc != nil {
		log.Printf("Running post boot function...")
		err := test.PostBootFunc(ctx)
		if err != nil {
			log.Printf("Post boot function had an error: %w", err)
			return err
		}
		log.Printf("Finished running post boot function!")
	}

	err = handler.StopInstances(ctx)
	if err != nil {
		log.Printf("Failed to destroy instance(s): %w", err)
		return err
	}

	if test.PostStopFunc != nil {
		log.Printf("Running post stop function...")
		err := test.PostStopFunc(ctx)
		if err != nil {
			log.Printf("Post stop function had an error: %w", err)
			return err
		}
		log.Printf("Finished running post stop function!")
	}

	log.Printf("Finished running %s test!", test.Name)

	return nil
}
