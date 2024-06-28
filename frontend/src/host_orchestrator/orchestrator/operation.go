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

	apiv1 "github.com/google/android-cuttlefish/frontend/src/liboperator/api/v1"

	"github.com/google/uuid"
)

type NotFoundOperationError string

func (s NotFoundOperationError) Error() string {
	return fmt.Sprintf("operation not found: %s", string(s))
}

type OperationWaitTimeoutError struct{}

func (s OperationWaitTimeoutError) Error() string { return "waiting for operation timed out" }

type OperationNotDoneError string

func (s OperationNotDoneError) Error() string {
	return fmt.Sprintf("operation %q is not done", string(s))
}

type OperationResult struct {
	Error error
	Value interface{}
}

type OperationManager interface {
	New() apiv1.Operation

	Get(name string) (apiv1.Operation, error)

	ListRunning() []apiv1.Operation

	GetResult(name string) (*OperationResult, error)

	Complete(name string, result *OperationResult) error

	// Waits for the specified operation to be DONE within the passed deadline. If the deadline
	// is reached `OperationWaitTimeoutError` will be returned.
	// NOTE: if dt is zero there's no timeout.
	Wait(name string, dt time.Duration) (*OperationResult, error)
}

type mapOMOperationEntry struct {
	op     apiv1.Operation
	result *OperationResult
	mutex  sync.RWMutex
	done   chan struct{}
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

func (m *MapOM) New() apiv1.Operation {
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
		op: apiv1.Operation{
			Name: name,
			Done: false,
		},
		mutex: sync.RWMutex{},
		done:  make(chan struct{}),
	}
	m.operations[name] = entry
	return entry.op
}

func (m *MapOM) ListRunning() []apiv1.Operation {
	m.mutex.RLock()
	defer m.mutex.RUnlock()
	result := []apiv1.Operation{}
	for _, v := range m.operations {
		if !v.op.Done {
			result = append(result, v.op)
		}
	}
	return result
}

func (m *MapOM) Get(name string) (apiv1.Operation, error) {
	entry, ok := m.getOperationEntry(name)
	if !ok {
		return apiv1.Operation{}, NotFoundOperationError(name)
	}
	entry.mutex.RLock()
	op := entry.op
	entry.mutex.RUnlock()
	return op, nil
}

func (m *MapOM) GetResult(name string) (*OperationResult, error) {
	entry, ok := m.getOperationEntry(name)
	if !ok {
		return nil, NotFoundOperationError(name)
	}
	entry.mutex.RLock()
	defer entry.mutex.RUnlock()
	if !entry.op.Done {
		return nil, OperationNotDoneError(name)
	}
	return entry.result, nil
}

func (m *MapOM) Complete(name string, result *OperationResult) error {
	entry, ok := m.getOperationEntry(name)
	if !ok {
		return fmt.Errorf("attempting to complete an operation which does not exist")
	}
	entry.mutex.Lock()
	defer entry.mutex.Unlock()
	entry.op.Done = true
	entry.result = result
	close(entry.done)
	return nil
}

func (m *MapOM) Wait(name string, dt time.Duration) (*OperationResult, error) {
	entry, ok := m.getOperationEntry(name)
	if !ok {
		return nil, NotFoundOperationError("map key didn't exist")
	}
	var timeoutCh <-chan time.Time
	if dt != 0 {
		timeoutCh = time.After(dt)
	}
	select {
	case <-entry.done:
		entry.mutex.RLock()
		result := entry.result
		entry.mutex.RUnlock()
		return result, nil
	case <-timeoutCh:
		return nil, new(OperationWaitTimeoutError)
	}
}

func (m *MapOM) getOperationEntry(name string) (*mapOMOperationEntry, bool) {
	m.mutex.RLock()
	defer m.mutex.RUnlock()
	op, ok := m.operations[name]
	return op, ok
}
