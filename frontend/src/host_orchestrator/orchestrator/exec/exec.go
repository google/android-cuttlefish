// Copyright 2022 Google LLC
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

package exec

import (
	"bytes"
	"context"
	"fmt"
	"os/exec"
	"strings"
)

type ExecContext = func(ctx context.Context, name string, args ...string) *exec.Cmd

// Executes a command with an execution context and return it standard output.
// Standard error is included in the error message if it fails.
func Exec(ctx ExecContext, name string, args ...string) (string, error) {
	cmd := ctx(context.TODO(), name, args...)
	var stdout, stderr bytes.Buffer
	cmd.Stdout = &stdout
	cmd.Stderr = &stderr
	err := cmd.Run()
	if err != nil {
		return stdout.String(),
			fmt.Errorf("execution of command %q with args %q failed: %w", name, strings.Join(args, " "), err)
	}
	return stdout.String(), nil
}
