// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//	http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package internal

import (
	"fmt"
	"log"
	"os/exec"
	"os/user"
	"syscall"
)

func CheckDeviceAccessible() error {
	u, err := user.Current()
	if err != nil {
		return fmt.Errorf("failed to get current user: %w", err)
	}
	username := u.Username

	// Try to start Podman socket on-demand
	ensurePodmanSocketRunning()

	// Verify user is registered in /etc/podcvd.users
	if _, err = readUserCidrFromConfig(username); err != nil {
		log.Printf("User setup is incomplete. Please execute `podcvd-setup` to configure necessary permissions and network ranges.")
		return fmt.Errorf("user %q is not registered in /etc/podcvd.users: %w", username, err)
	}

	// Check device permissions
	devices := []string{
		"/dev/kvm",
		"/dev/net/tun",
		"/dev/vhost-net",
		"/dev/vhost-vsock",
	}
	const R_OK = 4
	const W_OK = 2
	for _, device := range devices {
		if err := syscall.Access(device, R_OK|W_OK); err != nil {
			log.Printf("User setup is incomplete. Please execute `podcvd-setup` to configure necessary permissions.")
			return fmt.Errorf("device %q is not accessible (permission denied): %w", device, err)
		}
	}
	return nil
}

func ensurePodmanSocketRunning() {
	if err := exec.Command("systemctl", "--user", "start", "podman.socket").Run(); err != nil {
		log.Printf("Warning: failed to start podman.socket dynamically: %v", err)
	}
}
