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
	"sync"
	"fmt"

	"github.com/google/btree"
)

// Structure for managing the state of updated chunk for efficiently knowing whether the user
// artifact may need to calculate hash sum or not.
type ChunkState struct {
	fileSize int64
	items    *btree.BTree
	mutex    sync.RWMutex
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
	cs.items.ReplaceOrInsert(chunkStateItem{offset: 0, isUpdated: false})
	return &cs
}

func (cs *ChunkState) getItem(offset int64) *chunkStateItem {
	entry := cs.items.Get(chunkStateItem{offset: offset})
	if item, ok := entry.(chunkStateItem); ok {
		return &item
	} else {
		return nil
	}
}

func (cs *ChunkState) getPrevItem(offset int64) *chunkStateItem {
	var record *chunkStateItem
	cs.items.DescendLessOrEqual(chunkStateItem{offset: offset - 1}, func(item btree.Item) bool {
		entry := item.(chunkStateItem)
		record = &entry
		return false
	})
	return record
}

func (cs *ChunkState) getNextItem(offset int64) *chunkStateItem {
	var record *chunkStateItem
	cs.items.AscendGreaterOrEqual(chunkStateItem{offset: offset + 1}, func(item btree.Item) bool {
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
	cs.mutex.Lock()
	defer cs.mutex.Unlock()
	if item := cs.getItem(start); item != nil {
		if !item.isUpdated {
			cs.items.Delete(*item)
			if item.offset == 0 {
				cs.items.ReplaceOrInsert(chunkStateItem{offset: start, isUpdated: true})
			}
		}
	} else if prev := cs.getPrevItem(start); prev != nil {
		if !prev.isUpdated {
			cs.items.ReplaceOrInsert(chunkStateItem{offset: start, isUpdated: true})
		}
	} else {
		return fmt.Errorf("previous item should exist")
	}

	for next := cs.getNextItem(start); ; next = cs.getNextItem(start) {
		if next == nil {
			cs.items.ReplaceOrInsert(chunkStateItem{offset: end, isUpdated: false})
			break
		}
		if next.offset < end {
			cs.items.Delete(*next)
		} else if next.offset == end {
			if next.isUpdated {
				cs.items.Delete(*next)
			} else {
				break
			}
		} else {
			if next.isUpdated {
				cs.items.ReplaceOrInsert(chunkStateItem{offset: end, isUpdated: false})
			}
			break
		}
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
