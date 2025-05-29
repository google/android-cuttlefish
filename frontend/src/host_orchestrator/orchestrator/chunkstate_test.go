// Copyright 2025 Google LLC
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
	"testing"

	"github.com/google/btree"
	"github.com/google/go-cmp/cmp"
)

func getChunkStateItemList(cs *ChunkState) []chunkStateItem {
	items := []chunkStateItem{}
	cs.items.Ascend(func(item btree.Item) bool {
		items = append(items, item.(chunkStateItem))
		return true
	})
	return items
}

func TestChunkStateUpdateSucceedsForSeparatedChunks(t *testing.T) {
	cs := NewChunkState(100)
	cs.Update(0, 10)
	cs.Update(40, 50)
	cs.Update(90, 100)
	items := getChunkStateItemList(cs)
	expected := []chunkStateItem{
		{offset: 0, isUpdated: true},
		{offset: 10, isUpdated: false},
		{offset: 40, isUpdated: true},
		{offset: 50, isUpdated: false},
		{offset: 90, isUpdated: true},
		{offset: 100, isUpdated: false},
	}
	if diff := cmp.Diff(expected, items, cmp.AllowUnexported(chunkStateItem{})); diff != "" {
		t.Fatalf("chunk state mismatch (-want +got):\n%s", diff)
	}
}

func TestChunkStateUpdateSucceedsForSubranges(t *testing.T) {
	cs := NewChunkState(100)
	cs.Update(40, 50)
	cs.Update(60, 70)
	cs.Update(30, 80)
	cs.Update(50, 60)
	cs.Update(65, 75)
	items := getChunkStateItemList(cs)
	expected := []chunkStateItem{
		{offset: 30, isUpdated: true},
		{offset: 80, isUpdated: false},
	}
	if diff := cmp.Diff(expected, items, cmp.AllowUnexported(chunkStateItem{})); diff != "" {
		t.Fatalf("chunk state mismatch (-want +got):\n%s", diff)
	}
}

func TestChunkStateUpdateSucceedsForOverlappingRanges(t *testing.T) {
	cs := NewChunkState(100)
	cs.Update(10, 30)
	cs.Update(20, 40)
	cs.Update(70, 90)
	cs.Update(60, 80)
	items := getChunkStateItemList(cs)
	expected := []chunkStateItem{
		{offset: 10, isUpdated: true},
		{offset: 40, isUpdated: false},
		{offset: 60, isUpdated: true},
		{offset: 90, isUpdated: false},
	}
	if diff := cmp.Diff(expected, items, cmp.AllowUnexported(chunkStateItem{})); diff != "" {
		t.Fatalf("chunk state mismatch (-want +got):\n%s", diff)
	}
}

func TestChunkStateUpdateSucceedsForSameStartOrEnd(t *testing.T) {
	cs := NewChunkState(100)
	cs.Update(10, 15)
	cs.Update(10, 12)
	cs.Update(20, 22)
	cs.Update(20, 25)
	cs.Update(55, 60)
	cs.Update(58, 60)
	cs.Update(65, 70)
	cs.Update(68, 70)
	items := getChunkStateItemList(cs)
	expected := []chunkStateItem{
		{offset: 10, isUpdated: true},
		{offset: 15, isUpdated: false},
		{offset: 20, isUpdated: true},
		{offset: 25, isUpdated: false},
		{offset: 55, isUpdated: true},
		{offset: 60, isUpdated: false},
		{offset: 65, isUpdated: true},
		{offset: 70, isUpdated: false},
	}
	if diff := cmp.Diff(expected, items, cmp.AllowUnexported(chunkStateItem{})); diff != "" {
		t.Fatalf("chunk state mismatch (-want +got):\n%s", diff)
	}
}

func TestChunkStateUpdateSucceedsForMergingChunks(t *testing.T) {
	cs := NewChunkState(100)
	cs.Update(10, 20)
	cs.Update(30, 40)
	cs.Update(50, 60)
	cs.Update(20, 30)
	cs.Update(40, 50)
	items := getChunkStateItemList(cs)
	expected := []chunkStateItem{
		{offset: 10, isUpdated: true},
		{offset: 60, isUpdated: false},
	}
	if diff := cmp.Diff(expected, items, cmp.AllowUnexported(chunkStateItem{})); diff != "" {
		t.Fatalf("chunk state mismatch (-want +got):\n%s", diff)
	}
}

func TestChunkStateUpdateFailsForInvalidRanges(t *testing.T) {
	cs := NewChunkState(100)
	if err := cs.Update(-1, 10); err == nil {
		t.Fatalf("expected an error")
	}
	if err := cs.Update(90, 101); err == nil {
		t.Fatalf("expected an error")
	}
	if err := cs.Update(50, 50); err == nil {
		t.Fatalf("expected an error")
	}
	if err := cs.Update(80, 40); err == nil {
		t.Fatalf("expected an error")
	}
	items := getChunkStateItemList(cs)
	expected := []chunkStateItem{}
	if diff := cmp.Diff(expected, items, cmp.AllowUnexported(chunkStateItem{})); diff != "" {
		t.Fatalf("chunk state mismatch (-want +got):\n%s", diff)
	}
}

func TestChunkStateIsCompleted(t *testing.T) {
	cs := NewChunkState(100)
	cs.Update(0, 20)
	cs.Update(80, 100)
	cs.Update(20, 80)
	if !cs.IsCompleted() {
		t.Fatalf("expected as completed")
	}
}

func TestChunkStateIsNotCompletedWithMissingStart(t *testing.T) {
	cs := NewChunkState(100)
	cs.Update(1, 100)
	if cs.IsCompleted() {
		t.Fatalf("expected as not completed")
	}
}

func TestChunkStateIsNotCompletedWithMissingIntermediate(t *testing.T) {
	cs := NewChunkState(100)
	cs.Update(0, 20)
	cs.Update(80, 100)
	cs.Update(20, 79)
	if cs.IsCompleted() {
		t.Fatalf("expected as not completed")
	}
}

func TestChunkStateIsNotCompletedWithMissingEnd(t *testing.T) {
	cs := NewChunkState(100)
	cs.Update(0, 99)
	if cs.IsCompleted() {
		t.Fatalf("expected as not completed")
	}
}
