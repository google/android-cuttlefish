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
	"errors"
	"testing"
	"time"
)

func TestMapOMNewOperation(t *testing.T) {
	opName := "operation-1"
	om := NewMapOM(func() string { return opName })
	expectedOp := Operation{
		Name:   opName,
		Done:   false,
		Result: OperationResult{},
	}

	op := om.New()

	if op != expectedOp {
		t.Errorf("expected <<%+v>>, got %+v", expectedOp, op)
	}
}

func TestMapOMNewOperationNoNewUniqueUUIDsPanic(t *testing.T) {
	defer func() { _ = recover() }()
	om := NewMapOM(func() string { return "sameuuid" })

	om.New()
	om.New()

	t.Error("did not panic")
}

func TestMapOMGetOperation(t *testing.T) {
	opName := "operation-1"
	uuidFactory := func() string { return opName }

	t.Run("exists", func(t *testing.T) {
		om := NewMapOM(uuidFactory)
		om.New()

		op, err := om.Get(opName)

		if err != nil {
			t.Errorf("expected no error")
		}
		if op.Name != opName {
			t.Errorf("expected <<%q>>, got %q", opName, op.Name)
		}
	})

	t.Run("does not exist", func(t *testing.T) {
		om := NewMapOM(uuidFactory)

		op, err := om.Get(opName)

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
	opName := "operation-1"
	uuidFactory := func() string { return opName }

	t.Run("exists", func(t *testing.T) {
		om := NewMapOM(uuidFactory)
		om.New()
		result := OperationResult{
			Error: OperationResultError{"error"},
		}

		err := om.Complete(opName, result)

		if err != nil {
			t.Error("expected no error")
		}
		op, err := om.Get(opName)
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
		om := NewMapOM(uuidFactory)
		result := OperationResult{
			Error: OperationResultError{"error"},
		}

		err := om.Complete(opName, result)

		if err == nil {
			t.Error("expected error")
		}
	})
}

func TestMapOMWaitOperation(t *testing.T) {
	opName := "operation-1"
	uuidFactory := func() string { return opName }
	result := OperationResult{
		Error: OperationResultError{"error"},
	}

	t.Run("operation was completed", func(t *testing.T) {
		om := NewMapOM(uuidFactory)
		om.New()
		om.Complete(opName, result)

		op, err, timeout := om.waitForOperation(opName, 1*time.Second)

		if timeout {
			t.Error("expected to stop waiting as operation was completed")
		}
		if err != nil {
			t.Errorf("expected nil error, got %+v", err)
		}
		expectedOp, _ := om.Get(opName)
		if op != expectedOp {
			t.Errorf("expected <<%+v>>, got %+v", expectedOp, op)
		}
	})

	t.Run("operation is not completed", func(t *testing.T) {
		om := NewMapOM(uuidFactory)
		om.New()

		op, err, timeout := om.waitForOperation(opName, 1*time.Second)

		if !timeout {
			t.Error("expected to continue waiting as operation has not been completed yet")
		}
		if err != nil {
			t.Errorf("expected nil error, got %+v", err)
		}
		if (op != Operation{}) {
			t.Error("expected empty operation")
		}
	})

	t.Run("operation does not exist", func(t *testing.T) {
		om := NewMapOM(uuidFactory)

		op, err, timeout := om.waitForOperation(opName, 1*time.Second)

		if timeout {
			t.Error("expected to never wait as operation did not exist")
		}
		if err == nil {
			t.Error("expected non nil error")
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

func (om *MapOM) waitForOperation(name string, duration time.Duration) (op Operation, err error, timeout bool) {
	okCh := make(chan Operation, 1)
	errCh := make(chan error, 1)

	go func() {
		if op, err := om.Wait(name); err != nil {
			errCh <- err
		} else {
			okCh <- op
		}
	}()

	select {
	case op = <-okCh:
		return
	case err = <-errCh:
		return
	case <-time.After(duration):
		timeout = true
		return
	}
}
