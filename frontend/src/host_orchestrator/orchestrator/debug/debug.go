// Copyright 2021 Google LLC
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

package debug

type StaticVariables struct {
	InitialCVDBinAndroidBuildID     string `json:"initial_cvd_bin_android_build_id"`
	InitialCVDBinAndroidBuildTarget string `json:"initial_cvd_bin_android_build_target"`
}

type DynamicVariables struct {
}

type Variables struct {
	*StaticVariables
}

type VariablesManager struct {
	static StaticVariables
}

func NewVariablesManager(static StaticVariables) *VariablesManager {
	return &VariablesManager{
		static: static,
	}
}

func (m *VariablesManager) GetVariables() *Variables {
	static := m.static
	return &Variables{&static}
}
