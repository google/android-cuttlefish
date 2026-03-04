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
	"os"
	"syscall"
)

func CheckDeviceAccessible() error {
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
			username := os.Getenv("USER")
			if username == "" {
				username = "${USER}"
			}
			log.Printf("please try executing `sudo setfacl -m u:%s:rw %s`", username, device)
			return fmt.Errorf("device %q is not accessible: %w", device, err)
		}
	}
	return nil
}
