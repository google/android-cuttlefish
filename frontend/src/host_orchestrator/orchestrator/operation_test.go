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
	"sort"
	"sync"
	"testing"
	"time"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/liboperator/api/v1"

	"github.com/google/go-cmp/cmp"
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

		if notFoundErr, ok := err.(NotFoundOperationError); !ok {
			t.Errorf("expected <<%T>>, got %T", notFoundErr, err)
		}

		if (op != apiv1.Operation{}) {
			t.Errorf("expected zero value %T", op)
		}
	})
}

func TestMapOMCompleteOperation(t *testing.T) {
	result := &OperationResult{Error: errors.New("error")}

	t.Run("exists", func(t *testing.T) {
		om := NewMapOM()
		newOp := om.New()

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
	})

	t.Run("does not exist", func(t *testing.T) {
		om := NewMapOM()

		err := om.Complete("foo", result)

		if err == nil {
			t.Error("expected error")
		}
	})
}

func TestMapOMWaitOperation(t *testing.T) {
	dt := 1 * time.Second
	result := &OperationResult{Value: "foo"}

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
				got, err := om.Wait(newOp.Name, dt)
				duration := time.Since(start)

				if err != nil {
					t.Errorf("expected nil error, got %+v", err)
				}
				if duration >= dt {
					t.Error("reached the wait deadline")
				}
				if diff := cmp.Diff(result.Value, got.Value); diff != "" {
					t.Errorf("result value mismatch (-want +got):\n%s", diff)
				}
			}()
		}
		om.Complete(newOp.Name, result)
		wg.Wait()
	})

	t.Run("operation is not completed and deadline is reached", func(t *testing.T) {
		var wg sync.WaitGroup
		om := NewMapOM()
		newOp := om.New()
		// Testing more than one Wait call at the same time.
		for i := 0; i < 10; i++ {
			wg.Add(1)
			go func() {
				defer wg.Done()

				start := time.Now()
				res, err := om.Wait(newOp.Name, dt)
				duration := time.Since(start)

				if duration < dt {
					t.Error("wait deadline was not reached")
				}
				if timedOutErr, ok := err.(*OperationWaitTimeoutError); !ok {
					t.Errorf("expected <<%T>>, got %T", timedOutErr, err)
				}
				if res != nil {
					t.Error("expected nil result")
				}
			}()
		}
		wg.Wait()
	})

	t.Run("operation does not exist", func(t *testing.T) {
		om := NewMapOM()

		start := time.Now()
		res, err := om.Wait("foo", dt)
		duration := time.Since(start)

		if duration >= dt {
			t.Error("reached the wait deadline")
		}
		if notFoundErr, ok := err.(NotFoundOperationError); !ok {
			t.Errorf("expected <<%T>>, got %T", notFoundErr, err)
		}
		if res != nil {
			t.Error("expected nil result")
		}
	})
}

func TestMapOMGetOperationResult(t *testing.T) {
	t.Run("does not exist", func(t *testing.T) {
		om := NewMapOM()

		_, err := om.GetResult("foo")

		if notFoundErr, ok := err.(NotFoundOperationError); !ok {
			t.Errorf("expected <<%T>>, got %T", notFoundErr, err)
		}
	})

	t.Run("exists not done", func(t *testing.T) {
		om := NewMapOM()
		op := om.New()

		_, err := om.GetResult(op.Name)

		if notDoneErr, ok := err.(OperationNotDoneError); !ok {
			t.Errorf("expected <<%T>>, got %T", notDoneErr, err)
		}
	})

	t.Run("exists done", func(t *testing.T) {
		om := NewMapOM()
		op := om.New()
		om.Complete(op.Name, &OperationResult{Value: "bar"})

		res, err := om.GetResult(op.Name)

		if err != nil {
			t.Errorf("expected no error")
		}
		if res.Error != nil {
			t.Errorf("expected no error")
		}
		if diff := cmp.Diff("bar", res.Value); diff != "" {
			t.Errorf("result value (-want +got):\n%s", diff)
		}
	})
}

func TestMapOMListRunningEmpty(t *testing.T) {
	om := NewMapOM()

	result := om.ListRunning()

	if len(result) != 0 {
		t.Error("expected empty slice")
	}
}

func TestMapOMListRunning(t *testing.T) {
	om := NewMapOM()
	op := om.New()
	om.Complete(op.Name, &OperationResult{})
	op = om.New()
	om.Complete(op.Name, &OperationResult{})
	op1 := om.New()
	op2 := om.New()

	result := om.ListRunning()

	if len(result) != 2 {
		t.Error("expected 2 operations")
	}
	expected := []string{op1.Name, op2.Name}
	sort.Strings(expected)
	names := []string{}
	for _, s := range result {
		names = append(names, s.Name)
	}
	sort.Strings(names)

	if diff := cmp.Diff(expected, names); diff != "" {
		t.Errorf("result mismatch (-want +got):\n%s", diff)
	}

}
