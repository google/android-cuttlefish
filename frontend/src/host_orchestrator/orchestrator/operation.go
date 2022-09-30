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

package orchestrator

import (
	"fmt"
	"sync"
	"time"

	"github.com/google/uuid"
)

type NotFoundOperationError string

func (s NotFoundOperationError) Error() string {
	return fmt.Sprintf("operation not found: %s", string(s))
}

type Operation struct {
	Name   string
	Done   bool
	Result OperationResult
}

type OperationResult struct {
	Error OperationResultError
}

type OperationResultError struct {
	ErrorMsg string
}

type OperationManager interface {
	New() Operation

	Get(name string) (Operation, error)

	Complete(name string, result OperationResult) error

	// Waits for the specified operation to be DONE within the passed deadline. If the deadline
	// is reached it will return the current state of the operation which might be DONE or not.
	Wait(name string, dt time.Duration) (Operation, error)
}

type mapOMOperationEntry struct {
	data  Operation
	mutex sync.RWMutex
	done  chan struct{}
}

type MapOM struct {
	uuidFactory func() string
	mutex       sync.RWMutex
	operations  map[string]*mapOMOperationEntry
}

func NewMapOM() *MapOM {
	return &MapOM{
		uuidFactory: func() string { return uuid.New().String() },
		mutex:       sync.RWMutex{},
		operations:  make(map[string]*mapOMOperationEntry),
	}
}

const mapOMNewUUIDRetryLimit = 100

func (m *MapOM) New() Operation {
	m.mutex.Lock()
	defer m.mutex.Unlock()
	name, retryCount := m.uuidFactory(), 0
	_, found := m.operations[name]
	for found && retryCount < mapOMNewUUIDRetryLimit {
		name, retryCount = m.uuidFactory(), retryCount+1
		_, found = m.operations[name]
	}
	if retryCount == mapOMNewUUIDRetryLimit {
		panic("uuid retry limit reached")
	}
	entry := &mapOMOperationEntry{
		data: Operation{
			Name:   name,
			Done:   false,
			Result: OperationResult{},
		},
		mutex: sync.RWMutex{},
		done:  make(chan struct{}),
	}
	m.operations[name] = entry
	return entry.data
}

func (m *MapOM) Get(name string) (Operation, error) {
	entry, ok := m.getOperationEntry(name)
	if !ok {
		return Operation{}, NotFoundOperationError("map key didn't exist")
	}
	entry.mutex.RLock()
	op := entry.data
	entry.mutex.RUnlock()
	return op, nil
}

func (m *MapOM) Complete(name string, result OperationResult) error {
	op, ok := m.getOperationEntry(name)
	if !ok {
		return fmt.Errorf("attempting to complete an operation which does not exist")
	}
	op.mutex.Lock()
	defer op.mutex.Unlock()
	op.data.Done = true
	op.data.Result = result
	close(op.done)
	return nil
}

func (m *MapOM) Wait(name string, dt time.Duration) (Operation, error) {
	entry, ok := m.getOperationEntry(name)
	if !ok {
		return Operation{}, NotFoundOperationError("map key didn't exist")
	}
	select {
	case <-entry.done:
	case <-time.After(time.Duration(dt)):
	}
	entry.mutex.RLock()
	op := entry.data
	entry.mutex.RUnlock()
	return op, nil
}

func (m *MapOM) getOperationEntry(name string) (*mapOMOperationEntry, bool) {
	m.mutex.RLock()
	defer m.mutex.RUnlock()
	op, ok := m.operations[name]
	return op, ok
}

func (d *Operation) IsError() bool {
	return d.Done && (d.Result.Error != OperationResultError{})
}
