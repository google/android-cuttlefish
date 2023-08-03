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
	registry *registry
}

func NewManager(rootDir string, uuidGen func() string) *Manager {
	return &Manager{
		rootDir:  rootDir,
		uuidGen:  uuidGen,
		registry: defaultRegistry,
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

func (h *Manager) GetCVDBundle(
	buildID, target string, extraOptions *ExtraCVDOptions, fetcher CVDBundleFetcher) (string, error) {
	outDir := fmt.Sprintf("%s/%s_%s__cvd", h.rootDir, buildID, target)
	if extraOptions != nil {
		// A custom cvd bundle puts artifacts from different builds into the final directory so it can only be reused
		// if the same arguments are used.
		outDir = fmt.Sprintf("%s/%s__custom_cvd", h.rootDir, h.uuidGen())
	}
	f := func(dir string) error {
		if err := fetcher.Fetch(dir, buildID, target, extraOptions); err != nil {
			return err
		}
		return nil
	}
	if err := h.registry.GetOrDownload(outDir, f); err != nil {
		return "", err
	}
	return outDir, nil
}

func (h *Manager) GetKernelBundle(buildID, target string, fetcher Fetcher) (string, error) {
	outDir := fmt.Sprintf("%s/%s_%s__kernel", h.rootDir, buildID, target)
	f := func(dir string) error {
		if err := createDir(outDir); err != nil {
			return err
		}
		if err := fetcher.Fetch(outDir, buildID, target, "bzImage"); err != nil {
			return err
		}
		if err := fetcher.Fetch(outDir, buildID, target, "initramfs.img"); err != nil {
			// Certain kernel builds do not have corresponding ramdisks.
			if apiErr, ok := err.(*BuildAPIError); ok && apiErr.Code != http.StatusNotFound {
				return err
			}
		}
		return nil
	}
	if err := h.registry.GetOrDownload(outDir, f); err != nil {
		return "", err
	}
	return outDir, nil
}

func (h *Manager) GetBootloaderBundle(buildID, target string, fetcher Fetcher) (string, error) {
	outDir := fmt.Sprintf("%s/%s_%s__bootloader", h.rootDir, buildID, target)
	f := func(dir string) error {
		if err := createDir(outDir); err != nil {
			return err
		}
		if err := fetcher.Fetch(outDir, buildID, target, "u-boot.rom"); err != nil {
			return err
		}
		return nil
	}
	if err := h.registry.GetOrDownload(outDir, f); err != nil {
		return "", err
	}
	return outDir, nil
}

type downloadResult struct {
	OutDir string
	Error  error
}

var defaultRegistry = newRegistry()

type registry struct {
	ctxs  map[string]*downloadContext
	mutex sync.Mutex
}

func newRegistry() *registry {
	return &registry{
		ctxs: make(map[string]*downloadContext),
	}
}

type downloadFunc func(dir string) error

// The download function `f` will be executed on the first call only for the given directory. Subsequent calls will
// get the same results as the first call.
func (r *registry) GetOrDownload(dir string, f downloadFunc) error {
	ctx := r.getDownloadCtx(dir)
	return ctx.Run(f)
}

func (r *registry) getDownloadCtx(dir string) *downloadContext {
	r.mutex.Lock()
	defer r.mutex.Unlock()
	ctx := r.ctxs[dir]
	if ctx == nil {
		ctx = &downloadContext{Dir: dir}
		r.ctxs[dir] = ctx
	}
	return ctx
}

type downloadContext struct {
	Dir string

	mutex sync.Mutex
	done  bool
	err   error
}

func (c *downloadContext) Run(f downloadFunc) error {
	c.mutex.Lock()
	defer c.mutex.Unlock()
	if c.done {
		return c.err
	}
	c.err = f(c.Dir)
	c.done = true
	return c.err
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
