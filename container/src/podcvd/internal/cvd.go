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
	"io"
	"regexp"
	"sort"
	"strings"
	"text/tabwriter"

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

// TODO(b/535387856): Gather information from `cvd fleet` rather than parsing
// `cvd ps`, as it makes the behavior of `podcvd ps` much more complicated.
func PrintPsOutputs(results []HostExecResult, out io.Writer) error {
	var allRows []map[string]string
	colMap := make(map[string]int)
	for _, res := range results {
		rows, cols, err := parsePsOutput(string(res.Stdout), res.IP)
		if err != nil {
			return fmt.Errorf("failed to parse cvd ps output from group %q: %w", res.GroupName, err)
		}
		allRows = append(allRows, rows...)
		for _, col := range cols {
			if _, exists := colMap[col.name]; !exists {
				colMap[col.name] = col.start
			}
		}
	}
	var colNames []string
	for name := range colMap {
		colNames = append(colNames, name)
	}
	sort.Slice(colNames, func(i, j int) bool {
		return colMap[colNames[i]] < colMap[colNames[j]]
	})
	return combinePsOutputs(allRows, colNames, out)
}

func updateIPAndPortString(data, ipAddr string) string {
	operatorIpAndPortOnHost := fmt.Sprintf("%s:%d", ipAddr, portOperatorHttpsOnHost)
	for _, host := range []string{"0.0.0.0", "localhost", "127.0.0.1"} {
		data = strings.ReplaceAll(data, fmt.Sprintf("%s:%d", host, portOperatorHttps), operatorIpAndPortOnHost)
		data = strings.ReplaceAll(data, host, ipAddr)
	}
	return data
}

var cvdPathRegex = regexp.MustCompile(`^/var/tmp/cvd/[0-9]+/[0-9]+/home`)

func updateStringOnCvdGroupJsonRaw(data, podcvdHomeDir, ipAddr string) string {
	data = updateIPAndPortString(data, ipAddr)
	return cvdPathRegex.ReplaceAllString(data, podcvdHomeDir)
}

type psColumn struct {
	name  string
	start int
}

var psHeaderRegex = regexp.MustCompile(`\S+`)

func parsePsHeader(headerLine string) ([]psColumn, error) {
	matches := psHeaderRegex.FindAllStringIndex(headerLine, -1)
	if len(matches) == 0 {
		return nil, fmt.Errorf("invalid header line %q: no column headers found", headerLine)
	}
	var cols []psColumn
	for _, m := range matches {
		cols = append(cols, psColumn{
			name:  headerLine[m[0]:m[1]],
			start: m[0],
		})
	}
	return cols, nil
}

func parsePsRow(line string, cols []psColumn, ipAddr string) map[string]string {
	rowMap := make(map[string]string)
	lineLen := len(line)
	for i, col := range cols {
		if col.start >= lineLen {
			rowMap[col.name] = ""
			continue
		}
		end := lineLen
		if i < len(cols)-1 && cols[i+1].start < lineLen {
			end = cols[i+1].start
		}
		rowMap[col.name] = updateIPAndPortString(strings.TrimSpace(line[col.start:end]), ipAddr)
	}
	return rowMap
}

func parsePsOutput(stdout, ipAddr string) ([]map[string]string, []psColumn, error) {
	lines := strings.Split(strings.TrimSpace(stdout), "\n")
	if len(lines) < 1 || lines[0] == "" {
		return nil, nil, fmt.Errorf("empty cvd ps output: missing header")
	}
	cols, err := parsePsHeader(lines[0])
	if err != nil {
		return nil, nil, err
	}
	var rows []map[string]string
	for _, line := range lines[1:] {
		rows = append(rows, parsePsRow(line, cols, ipAddr))
	}
	return rows, cols, nil
}

func combinePsOutputs(allRows []map[string]string, colNames []string, out io.Writer) error {
	w := tabwriter.NewWriter(out, 0, 0, 3, ' ', 0)
	fmt.Fprintln(w, strings.Join(colNames, "\t"))
	for _, rowMap := range allRows {
		var rowValues []string
		for _, colName := range colNames {
			rowValues = append(rowValues, rowMap[colName])
		}
		fmt.Fprintln(w, strings.Join(rowValues, "\t"))
	}
	if err := w.Flush(); err != nil {
		return fmt.Errorf("failed to flush output: %w", err)
	}
	return nil
}
