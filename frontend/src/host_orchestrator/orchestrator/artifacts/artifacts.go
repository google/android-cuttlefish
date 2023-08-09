// Copyright 2023 Google LLC
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

package artifacts

import (
	"fmt"
	"net/http"
	"os"
	"sync"
)

// Downloads and organizes the Build artifacts.
//
// Artifacts will be organized the following way:
//  1. $ROOT_DIR/<BUILD_ID>_<TARGET>__cvd will store a full download.
//  2. $ROOT_DIR/<BUILD_ID>_<TARGET>__kernel will store kernel artifacts only.
type Manager struct {
	rootDir  string
	uuidGen  func() string
	map_     map[string]*downloadArtifactsMapEntry
	mapMutex sync.Mutex
}

func NewManager(rootDir string, uuidGen func() string) *Manager {
	return &Manager{
		rootDir: rootDir,
		uuidGen: uuidGen,
		map_:    make(map[string]*downloadArtifactsMapEntry),
	}
}

type ExtraCVDOptions struct {
	SystemImgBuildID string
	SystemImgTarget  string
}

type CVDBundleFetcher interface {
	// Fetches all the necessary artifacts to launch a Cuttlefish device. It support downloading a system
	// image from a different build if the extraOptions is provided.
	Fetch(outDir, buildID, target string, extraOptions *ExtraCVDOptions) error
}

type Fetcher interface {
	// Fetches specific artifacts.
	Fetch(outDir, buildID, target string, artifacts ...string) error
}

type downloadArtifactsResult struct {
	OutDir string
	Error  error
}

type downloadArtifactsMapEntry struct {
	mutex  sync.Mutex
	result *downloadArtifactsResult
}

func (h *Manager) GetCVDBundle(
	buildID, target string, extraOptions *ExtraCVDOptions, fetcher CVDBundleFetcher) (string, error) {
	outDir := fmt.Sprintf("%s/%s_%s__cvd", h.rootDir, buildID, target)
	f := func() (string, error) {
		if extraOptions != nil {
			// A custom cvd bundle puts artifacts from different builds into the final directory so it can only be reused
			// if the same arguments are used.
			outDir = fmt.Sprintf("%s/%s__custom_cvd", h.rootDir, h.uuidGen())
		}
		if err := fetcher.Fetch(outDir, buildID, target, extraOptions); err != nil {
			return "", err
		}
		return outDir, nil
	}
	return h.syncDownload(outDir, f)
}

func (h *Manager) GetKernelBundle(buildID, target string, fetcher Fetcher) (string, error) {
	f := func() (string, error) {
		outDir := fmt.Sprintf("%s/%s_%s__kernel", h.rootDir, buildID, target)
		if err := createDir(outDir); err != nil {
			return "", err
		}
		if err := fetcher.Fetch(outDir, buildID, target, "bzImage"); err != nil {
			return "", err
		}
		if err := fetcher.Fetch(outDir, buildID, target, "initramfs.img"); err != nil {
			// Certain kernel builds do not have corresponding ramdisks.
			if apiErr, ok := err.(*BuildAPIError); ok && apiErr.Code != http.StatusNotFound {
				return "", err
			}
		}
		return outDir, nil
	}
	return h.syncDownload(buildID+target+"kernel", f)
}

func (h *Manager) GetBootloaderBundle(buildID, target string, fetcher Fetcher) (string, error) {
	f := func() (string, error) {
		outDir := fmt.Sprintf("%s/%s_%s__bootloader", h.rootDir, buildID, target)
		if err := createDir(outDir); err != nil {
			return "", err
		}
		if err := fetcher.Fetch(outDir, buildID, target, "u-boot.rom"); err != nil {
			return "", err
		}
		return outDir, nil
	}
	return h.syncDownload(buildID+target+"bootloader", f)
}

// Synchronizes downloads to avoid downloading same bundle more than once.
func (h *Manager) syncDownload(key string, downloadFunc func() (string, error)) (string, error) {
	entry := h.getMapEntry(key)
	entry.mutex.Lock()
	defer entry.mutex.Unlock()
	if entry.result != nil {
		return entry.result.OutDir, entry.result.Error
	}
	entry.result = &downloadArtifactsResult{}
	entry.result.OutDir, entry.result.Error = downloadFunc()
	return entry.result.OutDir, entry.result.Error
}

func (h *Manager) getMapEntry(key string) *downloadArtifactsMapEntry {
	h.mapMutex.Lock()
	defer h.mapMutex.Unlock()
	entry := h.map_[key]
	if entry == nil {
		entry = &downloadArtifactsMapEntry{}
		h.map_[key] = entry
	}
	return entry
}

// Fails if the directory already exists.
func createNewDir(dir string) error {
	err := os.Mkdir(dir, 0774)
	if err != nil {
		return err
	}
	// Sets dir permission regardless of umask.
	return os.Chmod(dir, 0774)
}

func createDir(dir string) error {
	if err := createNewDir(dir); os.IsExist(err) {
		return nil
	} else {
		return err
	}
}
