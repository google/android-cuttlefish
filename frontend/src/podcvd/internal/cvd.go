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

	"github.com/go-playground/validator/v10"
)

type Instance struct {
	AdbPort int `json:"adb_port"`
}

type InstanceGroup struct {
	GroupName string     `json:"group_name" validate:"required"`
	Instances []Instance `json:"instances" validate:"dive,required"`
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
