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
	"errors"
	"sync"
	"testing"
	"time"
)

func TestMapOMNewOperation(t *testing.T) {
	om := NewMapOM()

	op := om.New()

	if op.Name == "" {
		t.Error("expected non empty value")
	}
	if op.Done {
		t.Error("expected false")
	}
	if (op.Result != OperationResult{}) {
		t.Error("expected empty result")
	}
}

func TestMapOMNewOperationNoNewUniqueUUIDsPanic(t *testing.T) {
	defer func() { _ = recover() }()
	om := NewMapOM()
	om.uuidFactory = func() string { return "sameuuid" }

	om.New()
	om.New()

	t.Error("did not panic")
}

func TestMapOMGetOperation(t *testing.T) {
	t.Run("exists", func(t *testing.T) {
		om := NewMapOM()
		newOp := om.New()

		op, err := om.Get(newOp.Name)

		if err != nil {
			t.Errorf("expected no error")
		}
		if op.Name != newOp.Name {
			t.Errorf("expected <<%q>>, got %q", newOp.Name, op.Name)
		}
	})

	t.Run("does not exist", func(t *testing.T) {
		om := NewMapOM()

		op, err := om.Get("foo")

		if err == nil {
			t.Errorf("expected error")
			var notFoundError *NotFoundOperationError
			if !errors.As(err, &notFoundError) {
				t.Errorf("error type <<\"%T\">> not found in error chain", notFoundError)
			}
		}
		if (op != Operation{}) {
			t.Errorf("expected zero value %T", op)
		}
	})
}

func TestMapOMCompleteOperation(t *testing.T) {
	t.Run("exists", func(t *testing.T) {
		om := NewMapOM()
		newOp := om.New()
		result := OperationResult{
			Error: OperationResultError{"error"},
		}

		err := om.Complete(newOp.Name, result)

		if err != nil {
			t.Error("expected no error")
		}
		op, err := om.Get(newOp.Name)
		if err != nil {
			t.Error("expected no error")
		}
		if !op.Done {
			t.Error("expected true")
		}
		if op.Result != result {
			t.Errorf("expected <<%+v>>, got %+v", result, op.Result)
		}
	})

	t.Run("does not exist", func(t *testing.T) {
		om := NewMapOM()
		result := OperationResult{
			Error: OperationResultError{"error"},
		}

		err := om.Complete("foo", result)

		if err == nil {
			t.Error("expected error")
		}
	})
}

func TestMapOMWaitOperation(t *testing.T) {
	dt := 1 * time.Second
	result := OperationResult{
		Error: OperationResultError{"error"},
	}

	t.Run("operation was completed", func(t *testing.T) {
		var wg sync.WaitGroup
		om := NewMapOM()
		newOp := om.New()
		// Testing more than one Wait call at the same time.
		for i := 0; i < 10; i++ {
			wg.Add(1)
			go func() {
				defer wg.Done()

				start := time.Now()
				op, err := om.Wait(newOp.Name, dt)
				duration := time.Since(start)

				if err != nil {
					t.Errorf("expected nil error, got %+v", err)
				}
				if duration >= dt {
					t.Error("reached the wait deadline")
				}
				expectedOp, _ := om.Get(newOp.Name)
				if op != expectedOp {
					t.Errorf("expected <<%+v>>, got %+v", expectedOp, op)
				}
			}()
		}
		om.Complete(newOp.Name, result)
		wg.Wait()
	})

	t.Run("operation is not completed", func(t *testing.T) {
		var wg sync.WaitGroup
		om := NewMapOM()
		newOp := om.New()
		// Testing more than one Wait call at the same time.
		for i := 0; i < 10; i++ {
			wg.Add(1)
			go func() {
				defer wg.Done()

				start := time.Now()
				op, err := om.Wait(newOp.Name, dt)
				duration := time.Since(start)

				if err != nil {
					t.Errorf("expected nil error, got %+v", err)
				}
				if duration < dt {
					t.Error("wait deadline was not reached")
				}
				expectedOp, _ := om.Get(newOp.Name)
				if op != expectedOp {
					t.Errorf("expected <<%+v>>, got %+v", expectedOp, op)
				}
			}()
		}
		wg.Wait()
	})

	t.Run("operation does not exist", func(t *testing.T) {
		om := NewMapOM()

		start := time.Now()
		op, err := om.Wait("foo", dt)
		duration := time.Since(start)

		if duration >= dt {
			t.Error("reached the wait deadline")
		}
		if notFoundErr, ok := err.(NotFoundOperationError); !ok {
			t.Errorf("expected <<%T>>, got %T", notFoundErr, err)
		}
		if (op != Operation{}) {
			t.Error("expected empty operation")
		}
	})
}

func TestOperationIsError(t *testing.T) {
	var tests = []struct {
		op    Operation
		isErr bool
	}{
		{
			op: Operation{
				Name: "operation-1",
				Done: false,
				Result: OperationResult{
					Error: OperationResultError{"error"},
				},
			},
			isErr: false,
		},
		{
			op: Operation{
				Name:   "operation-1",
				Done:   true,
				Result: OperationResult{},
			},
			isErr: false,
		},
		{
			op: Operation{
				Name: "operation-1",
				Done: true,
				Result: OperationResult{
					Error: OperationResultError{"error"},
				},
			},
			isErr: true,
		},
	}

	for _, test := range tests {
		isErr := test.op.IsError()

		if isErr != test.isErr {
			t.Errorf("expected <<%v>>, got %v for operation %+v", test.isErr, isErr, test.op)
		}
	}
}
