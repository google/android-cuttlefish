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
)

func TestMapOMNewOperation(t *testing.T) {
	opName := "operation-1"
	uuidFactory := func() string { return opName }

	t.Run("succeeds", func(t *testing.T) {
		om := NewMapOM(uuidFactory)
		expectedOp := OperationData{
			Name: opName,
			Done: false,
		}

		op, _ := om.NewOperation()

		if op != expectedOp {
			t.Errorf("expected <<%+v>>, got %+v", expectedOp, op)
		}
	})

	t.Run("fails", func(t *testing.T) {
		om := NewMapOM(uuidFactory)

		om.NewOperation()
		_, err := om.NewOperation()

		var newOperationErr NewOperationError
		if !errors.As(err, &newOperationErr) {
			t.Errorf("error type <<\"%T\">> not found in error chain", newOperationErr)
		}
	})
}

func TestMapOMGetOperation(t *testing.T) {
	opName := "operation-1"
	uuidFactory := func() string { return opName }

	t.Run("exists", func(t *testing.T) {
		om := NewMapOM(uuidFactory)
		expectedOp := OperationData{
			Name: opName,
			Done: false,
		}
		om.NewOperation()

		op, ok := om.GetOperation(opName)

		if ok != true {
			t.Errorf("expected true")
		}
		if op != expectedOp {
			t.Errorf("expected <<%+v>>, got %+v", expectedOp, op)
		}
	})

	t.Run("does not exist", func(t *testing.T) {
		om := NewMapOM(uuidFactory)

		op, ok := om.GetOperation(opName)

		if ok != false {
			t.Errorf("expected false")
		}
		if (op != OperationData{}) {
			t.Errorf("expected zero value %T", op)
		}
	})
}

func TestMapOMCompleteOperation(t *testing.T) {
	opName := "operation-1"
	uuidFactory := func() string { return opName }

	t.Run("exists", func(t *testing.T) {
		om := NewMapOM(uuidFactory)
		om.NewOperation()
		result := OperationResultData{
			Error: OperationErrorData{"error"},
		}
		expectedOp := OperationData{
			Name: opName,
			Done: true,
			Result: OperationResultData{
				Error: OperationErrorData{"error"},
			},
		}

		om.CompleteOperation(opName, result)

		op, ok := om.GetOperation(opName)
		if ok != true {
			t.Errorf("expected true")
		}
		if op != expectedOp {
			t.Errorf("expected <<%+v>>, got %+v", expectedOp, op)
		}
	})

	t.Run("does not exist", func(t *testing.T) {
		om := NewMapOM(uuidFactory)
		result := OperationResultData{
			Error: OperationErrorData{"error"},
		}

		om.CompleteOperation(opName, result)

		_, ok := om.GetOperation(opName)
		if ok != false {
			t.Errorf("expected false")
		}
	})
}

func TestOperationDataIsError(t *testing.T) {
	var tests = []struct {
		op    OperationData
		isErr bool
	}{
		{
			op: OperationData{
				Name: "operation-1",
				Done: false,
				Result: OperationResultData{
					Error: OperationErrorData{"error"},
				},
			},
			isErr: false,
		},
		{
			op: OperationData{
				Name:   "operation-1",
				Done:   true,
				Result: OperationResultData{},
			},
			isErr: false,
		},
		{
			op: OperationData{
				Name: "operation-1",
				Done: true,
				Result: OperationResultData{
					Error: OperationErrorData{"error"},
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
