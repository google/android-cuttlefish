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

type OperationManager interface {
	New() apiv1.Operation

	Get(name string) (apiv1.Operation, error)

	Complete(name string, result apiv1.OperationResult) error

	// Waits for the specified operation to be DONE within the passed deadline. If the deadline
	// is reached `OperationWaitTimeoutError` will be returned.
	Wait(name string, dt time.Duration) (apiv1.Operation, error)
}

type mapOMOperationEntry struct {
	data  apiv1.Operation
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
		data: apiv1.Operation{
			Name: name,
			Done: false,
		},
		mutex: sync.RWMutex{},
		done:  make(chan struct{}),
	}
	m.operations[name] = entry
	return entry.data
}

func (m *MapOM) Get(name string) (apiv1.Operation, error) {
	entry, ok := m.getOperationEntry(name)
	if !ok {
		return apiv1.Operation{}, NotFoundOperationError("map key didn't exist")
	}
	entry.mutex.RLock()
	op := entry.data
	entry.mutex.RUnlock()
	return op, nil
}

func (m *MapOM) Complete(name string, result apiv1.OperationResult) error {
	op, ok := m.getOperationEntry(name)
	if !ok {
		return fmt.Errorf("attempting to complete an operation which does not exist")
	}
	op.mutex.Lock()
	defer op.mutex.Unlock()
	op.data.Done = true
	op.data.Result = &result
	close(op.done)
	return nil
}

func (m *MapOM) Wait(name string, dt time.Duration) (apiv1.Operation, error) {
	entry, ok := m.getOperationEntry(name)
	if !ok {
		return apiv1.Operation{}, NotFoundOperationError("map key didn't exist")
	}
	select {
	case <-entry.done:
		entry.mutex.RLock()
		op := entry.data
		entry.mutex.RUnlock()
		return op, nil
	case <-time.After(time.Duration(dt)):
		return apiv1.Operation{}, new(OperationWaitTimeoutError)
	}
}

func (m *MapOM) getOperationEntry(name string) (*mapOMOperationEntry, bool) {
	m.mutex.RLock()
	defer m.mutex.RUnlock()
	op, ok := m.operations[name]
	return op, ok
}

func isFailed(op *apiv1.Operation) bool {
	return op.Done && op.Result != nil && op.Result.Error != nil
}
