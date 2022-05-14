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

package main

import (
	"fmt"
	"log"
	"sync"
)

type NewOperationError string

func (s NewOperationError) Error() string {
	return fmt.Sprintf("failure creating a new operation: %s", string(s))
}

type OperationData struct {
	Name   string
	Done   bool
	Result OperationResultData
}

type OperationResultData struct {
	Error OperationErrorData
	OK    OperationOKData
}

type OperationErrorData struct {
	ErrorMsg string
}

type OperationOKData struct{}

type OperationManager interface {
	NewOperation() (OperationData, error)

	GetOperation(string) (OperationData, bool)

	CompleteOperation(string, OperationResultData)
}

type MapOM struct {
	uuidFactory func() string
	mutex       sync.RWMutex
	operations  map[string]OperationData
}

func NewMapOM(uuidFactory func() string) *MapOM {
	return &MapOM{
		uuidFactory: uuidFactory,
		mutex:       sync.RWMutex{},
		operations:  make(map[string]OperationData),
	}
}

func (m *MapOM) NewOperation() (OperationData, error) {
	m.mutex.Lock()
	defer m.mutex.Unlock()
	name := m.uuidFactory()
	if _, ok := m.operations[name]; ok {
		return OperationData{}, NewOperationError("new operation name already exists")
	}
	op := OperationData{name, false, OperationResultData{}}
	m.operations[name] = op
	return op, nil
}

func (m *MapOM) GetOperation(name string) (OperationData, bool) {
	m.mutex.RLock()
	defer m.mutex.RUnlock()
	op, ok := m.operations[name]
	return op, ok
}

func (m *MapOM) CompleteOperation(name string, result OperationResultData) {
	m.mutex.Lock()
	defer m.mutex.Unlock()
	op, ok := m.operations[name]
	if !ok {
		log.Println("complete operation error: attempting to complete an operation which does not exist")
		return
	}
	op.Done = true
	op.Result = result
	m.operations[name] = op
}

func (d *OperationData) IsError() bool {
	return d.Done && (d.Result.Error != OperationErrorData{})
}
