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
	"encoding/json"
	"fmt"
	"regexp"
	"strings"

	"github.com/go-playground/validator/v10"
)

type Instance struct {
	AdbPort int `json:"adb_port"`
}

type InstanceGroup struct {
	GroupName string     `json:"group_name" validate:"required"`
	Instances []Instance `json:"instances" validate:"dive,required"`
}

type InstanceGroups struct {
	Groups []InstanceGroup `json:"groups" validate:"dive,required"`
}

func ParseInstanceGroup(jsonStr, groupName string) (*InstanceGroup, error) {
	var instanceGroup InstanceGroup
	if err := json.Unmarshal([]byte(jsonStr), &instanceGroup); err != nil {
		return nil, fmt.Errorf("failed to parse JSON object: %w", err)
	}
	validate := validator.New()
	if err := validate.Struct(instanceGroup); err != nil {
		return nil, fmt.Errorf("invalid JSON object: %w", err)
	}
	if instanceGroup.GroupName != groupName {
		return nil, fmt.Errorf("unexpected group name observed while parsing JSON object (expected: %q, actual: %q)", groupName, instanceGroup.GroupName)
	}
	return &instanceGroup, nil
}

func ParseInstanceGroups(jsonStr, groupName string) (*InstanceGroup, error) {
	var instanceGroups InstanceGroups
	if err := json.Unmarshal([]byte(jsonStr), &instanceGroups); err != nil {
		return nil, fmt.Errorf("failed to parse JSON object: %w", err)
	}
	validate := validator.New()
	if err := validate.Struct(instanceGroups); err != nil {
		return nil, fmt.Errorf("invalid JSON object: %w", err)
	}
	if len(instanceGroups.Groups) != 1 {
		return nil, fmt.Errorf("unexpected number of groups observed while parsing JSON object: %d", len(instanceGroups.Groups))
	}
	instanceGroup := instanceGroups.Groups[0]
	if instanceGroup.GroupName != groupName {
		return nil, fmt.Errorf("unexpected group name observed while parsing JSON object (expected: %q, actual: %q)", groupName, instanceGroup.GroupName)
	}
	return &instanceGroup, nil
}

func UpdateCvdGroupJsonRaw(data any, podcvdHomeDir, ipAddr string) {
	switch v := data.(type) {
	case map[string]any:
		for k, val := range v {
			if s, ok := val.(string); ok {
				v[k] = updateStringOnCvdGroupJsonRaw(s, podcvdHomeDir, ipAddr)
			} else {
				UpdateCvdGroupJsonRaw(val, podcvdHomeDir, ipAddr)
			}
		}
	case []any:
		for k, val := range v {
			if s, ok := val.(string); ok {
				v[k] = updateStringOnCvdGroupJsonRaw(s, podcvdHomeDir, ipAddr)
			} else {
				UpdateCvdGroupJsonRaw(val, podcvdHomeDir, ipAddr)
			}
		}
	}
}

var cvdPathRegex = regexp.MustCompile(`^/var/tmp/cvd/[0-9]+/[0-9]+/home`)

func updateStringOnCvdGroupJsonRaw(data, podcvdHomeDir, ipAddr string) string {
	data = strings.ReplaceAll(data, "0.0.0.0", ipAddr)
	data = strings.ReplaceAll(data, "localhost", ipAddr)
	data = strings.ReplaceAll(data, "127.0.0.1", ipAddr)
	if cvdPathRegex.MatchString(data) {
		data = cvdPathRegex.ReplaceAllString(data, podcvdHomeDir)
	}
	return data
}
