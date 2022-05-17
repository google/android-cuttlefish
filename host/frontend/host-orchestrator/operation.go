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
	New() (OperationData, error)

	Get(string) (OperationData, bool)

	Complete(string, OperationResultData)

	// TODO(b/231319087) This should handle timeout.
	Wait(string)
}

type mapOMOperationEntry struct {
	data  OperationData
	mutex sync.RWMutex
	wait  sync.WaitGroup
}

type MapOM struct {
	uuidFactory func() string
	mutex       sync.RWMutex
	operations  map[string]*mapOMOperationEntry
}

func NewMapOM(uuidFactory func() string) *MapOM {
	return &MapOM{
		uuidFactory: uuidFactory,
		mutex:       sync.RWMutex{},
		operations:  make(map[string]*mapOMOperationEntry),
	}
}

func (m *MapOM) New() (OperationData, error) {
	m.mutex.Lock()
	defer m.mutex.Unlock()
	name := m.uuidFactory()
	if _, ok := m.operations[name]; ok {
		return OperationData{}, NewOperationError("new operation name already exists")
	}
	wg := sync.WaitGroup{}
	wg.Add(1)
	op := &mapOMOperationEntry{
		data:  OperationData{name, false, OperationResultData{}},
		mutex: sync.RWMutex{},
		wait:  wg,
	}
	m.operations[name] = op
	return op.data, nil
}

func (m *MapOM) Get(name string) (OperationData, bool) {
	op, ok := m.getOperationEntry(name)
	if !ok {
		return OperationData{}, false
	}
	op.mutex.RLock()
	defer op.mutex.RUnlock()
	return op.data, true
}

func (m *MapOM) Complete(name string, result OperationResultData) {
	op, ok := m.getOperationEntry(name)
	if !ok {
		log.Println("complete operation error: attempting to complete an operation which does not exist")
		return
	}
	op.mutex.Lock()
	defer op.mutex.Unlock()
	op.wait.Done()
	op.data.Done = true
	op.data.Result = result
}

func (m *MapOM) Wait(name string) {
	op, ok := m.getOperationEntry(name)
	if !ok {
		return
	}
	op.wait.Wait()
}

func (m *MapOM) getOperationEntry(name string) (*mapOMOperationEntry, bool) {
	m.mutex.RLock()
	defer m.mutex.RUnlock()
	op, ok := m.operations[name]
	return op, ok
}

func (d *OperationData) IsError() bool {
	return d.Done && (d.Result.Error != OperationErrorData{})
}
