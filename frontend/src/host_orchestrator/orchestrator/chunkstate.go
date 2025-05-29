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
	"fmt"
	"sync"

	"github.com/google/btree"
)

// Structure for managing the state of updated chunk for efficiently knowing whether the user
// artifact may need to calculate hash sum or not.
type ChunkState struct {
	fileSize int64
	// Items are stored with a sequence of alternating true/false isUpdated field and increasing
	// offset field.
	items *btree.BTree
	mutex sync.RWMutex
}

type chunkStateItem struct {
	// Starting byte offset of the continuous range having same state whether updated or not.
	offset int64
	// State description whether given byte offset of the user artifact is updated or not.
	isUpdated bool
}

func (i chunkStateItem) Less(item btree.Item) bool {
	return i.offset < item.(chunkStateItem).offset
}

func NewChunkState(fileSize int64) *ChunkState {
	cs := ChunkState{
		fileSize: fileSize,
		items:    btree.New(2),
		mutex:    sync.RWMutex{},
	}
	return &cs
}

func (cs *ChunkState) getItemOrPrev(offset int64) *chunkStateItem {
	var record *chunkStateItem
	cs.items.DescendLessOrEqual(chunkStateItem{offset: offset}, func(item btree.Item) bool {
		entry := item.(chunkStateItem)
		record = &entry
		return false
	})
	return record
}

func (cs *ChunkState) getItemOrNext(offset int64) *chunkStateItem {
	var record *chunkStateItem
	cs.items.AscendGreaterOrEqual(chunkStateItem{offset: offset}, func(item btree.Item) bool {
		entry := item.(chunkStateItem)
		record = &entry
		return false
	})
	return record
}

func (cs *ChunkState) Update(start int64, end int64) error {
	if start < 0 {
		return fmt.Errorf("invalid start offset of the range")
	}
	if end > cs.fileSize {
		return fmt.Errorf("invalid end offset of the range")
	}
	if start >= end {
		return fmt.Errorf("start offset should be less than end offset")
	}
	cs.mutex.Lock()
	defer cs.mutex.Unlock()

	// Remove all current state between the start offset and the end offset.
	for item := cs.getItemOrNext(start); item != nil && item.offset <= end; item = cs.getItemOrNext(start) {
		cs.items.Delete(*item)
	}
	// State of the start offset is updated according to the state of the previous item.
	if prev := cs.getItemOrPrev(start); prev == nil || !prev.isUpdated {
		cs.items.ReplaceOrInsert(chunkStateItem{offset: start, isUpdated: true})
	}
	// State of the end offset is updated according to the state of the next item.
	if next := cs.getItemOrNext(end); next == nil || next.isUpdated {
		cs.items.ReplaceOrInsert(chunkStateItem{offset: end, isUpdated: false})
	}
	return nil
}

func (cs *ChunkState) IsCompleted() bool {
	cs.mutex.RLock()
	defer cs.mutex.RUnlock()
	if cs.items.Len() != 2 {
		return false
	}
	first := chunkStateItem{offset: 0, isUpdated: true}
	last := chunkStateItem{offset: cs.fileSize, isUpdated: false}
	return cs.items.Min() == first && cs.items.Max() == last
}
