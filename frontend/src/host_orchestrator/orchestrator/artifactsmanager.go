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

package orchestrator

import (
	"fmt"
	"net/http"
	"sync"
)

// Downloads and organizes the Build artifacts.
//
// Artifacts will be organized the following way:
//  1. $ROOT_DIR/<BUILD_ID>_<TARGET>__cvd will store a full download.
//  2. $ROOT_DIR/<BUILD_ID>_<TARGET>__kernel will store kernel artifacts only.
type ArtifactsManager struct {
	rootDir  string
	uuidGen  func() string
	map_     map[string]*downloadArtifactsMapEntry
	mapMutex sync.Mutex
}

func NewArtifactsManager(rootDir string, uuidGen func() string) *ArtifactsManager {
	return &ArtifactsManager{
		rootDir: rootDir,
		uuidGen: uuidGen,
		map_:    make(map[string]*downloadArtifactsMapEntry),
	}
}

type ExtraCVDOptions struct {
	SystemImgBuildID string
	SystemImgTarget  string
}

type ArtifactsFetcher interface {
	// Fetches all artifacts necessary to launch a CVD. It support downloading a system
	// image from a different build if the extraOptions is provided.
	FetchCVD(outDir, buildID, target string, extraOptions *ExtraCVDOptions) error
	// Fetches specific artifacts from the build API.
	FetchArtifacts(outDir, buildID, target string, artifacts ...string) error
}

type downloadArtifactsResult struct {
	OutDir string
	Error  error
}

type downloadArtifactsMapEntry struct {
	mutex  sync.Mutex
	result *downloadArtifactsResult
}

func (h *ArtifactsManager) GetCVDBundle(buildID, target string, extraOptions *ExtraCVDOptions, fetcher ArtifactsFetcher) (string, error) {
	outDir := fmt.Sprintf("%s/%s_%s__cvd", h.rootDir, buildID, target)
	f := func() (string, error) {
		if extraOptions != nil {
			// A custom cvd bundle puts artifacts from different builds into the final directory so it can only be reused
			// if the same arguments are used.
			outDir = fmt.Sprintf("%s/%s__custom_cvd", h.rootDir, h.uuidGen())
		}
		if err := fetcher.FetchCVD(outDir, buildID, target, extraOptions); err != nil {
			return "", err
		}
		return outDir, nil
	}
	return h.syncDownload(outDir, f)
}

func (h *ArtifactsManager) GetKernelBundle(buildID, target string, fetcher ArtifactsFetcher) (string, error) {
	f := func() (string, error) {
		outDir := fmt.Sprintf("%s/%s_%s__kernel", h.rootDir, buildID, target)
		if err := createDir(outDir); err != nil {
			return "", err
		}
		if err := fetcher.FetchArtifacts(outDir, buildID, target, "bzImage"); err != nil {
			return "", err
		}
		if err := fetcher.FetchArtifacts(outDir, buildID, target, "initramfs.img"); err != nil {
			// Certain kernel builds do not have corresponding ramdisks.
			if apiErr, ok := err.(*BuildAPIError); ok && apiErr.Code != http.StatusNotFound {
				return "", err
			}
		}
		return outDir, nil
	}
	return h.syncDownload(buildID+target+"kernel", f)
}

func (h *ArtifactsManager) GetBootloaderBundle(buildID, target string, fetcher ArtifactsFetcher) (string, error) {
	f := func() (string, error) {
		outDir := fmt.Sprintf("%s/%s_%s__bootloader", h.rootDir, buildID, target)
		if err := createDir(outDir); err != nil {
			return "", err
		}
		if err := fetcher.FetchArtifacts(outDir, buildID, target, "u-boot.rom"); err != nil {
			return "", err
		}
		return outDir, nil
	}
	return h.syncDownload(buildID+target+"bootloader", f)
}

// Synchronizes downloads to avoid downloading same bundle more than once.
func (h *ArtifactsManager) syncDownload(key string, downloadFunc func() (string, error)) (string, error) {
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

func (h *ArtifactsManager) getMapEntry(key string) *downloadArtifactsMapEntry {
	h.mapMutex.Lock()
	defer h.mapMutex.Unlock()
	entry := h.map_[key]
	if entry == nil {
		entry = &downloadArtifactsMapEntry{}
		h.map_[key] = entry
	}
	return entry
}
