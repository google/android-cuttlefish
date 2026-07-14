// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package main

import (
	"context"
	"encoding/json"
	"errors"
	"io"
	"log"
	"net/http"
	"testing"
	"time"

	"github.com/google/android-cuttlefish/e2etests/orchestration/common"
	hoapi "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	hoclient "github.com/google/android-cuttlefish/frontend/src/libhoclient"
)

const baseURL = "http://0.0.0.0:2080"

func TestInstanceLogs(t *testing.T) {
	srv := hoclient.NewHostOrchestratorClient(baseURL)
	t.Cleanup(func() {
		if err := common.CollectHOLogs(baseURL); err != nil {
			log.Printf("failed to collect HO logs: %s", err)
		}
	})
	config := `
  {
    "instances": [
      {
        "vm": {
					"crosvm": {
						"enable_sandbox": false
					},
          "memory_mb": 8192,
          "setupwizard_mode": "OPTIONAL",
          "cpus": 8
        },
        "disk": {
	 				"default_build": "@ab/aosp-android-latest-release/aosp_cf_x86_64_only_phone-userdebug",
					"download_img_zip": true
        }
      }
    ]
  }
  `
	envConfig := make(map[string]interface{})
	if err := json.Unmarshal([]byte(config), &envConfig); err != nil {
		t.Fatal(err)
	}
	createReq := &hoapi.CreateCVDRequest{
		EnvConfig: envConfig,
	}
	if _, err := srv.CreateCVD(createReq, &hoclient.AccessTokenBuildAPICreds{}); err != nil {
		t.Fatal(err)
	}
	oneSecStreamChan := make(chan result)
	twoSecsStreamChan := make(chan result)
	go stream(1*time.Second, oneSecStreamChan)
	go stream(30*time.Second, twoSecsStreamChan)
	oneSecRes := <-oneSecStreamChan
	twoSecsRes := <-twoSecsStreamChan

	if oneSecRes.err != nil {
		t.Fatal(oneSecRes.err)
	}
	if twoSecsRes.err != nil {
		t.Fatal(twoSecsRes.err)
	}
	if oneSecRes.read == 0 {
		t.Fatal("0 bytes read")
	}
	diff := twoSecsRes.read - oneSecRes.read
	delta := 50
	if diff <= delta {
		t.Fatalf("expected stream delta bigger than: %d, got: %d", delta, diff)
	}

	// Verifies stream stops when device is deleted
	stopStreamChan := make(chan result)
	start := time.Now()
	go stream(1*time.Minute, stopStreamChan)
	go func() {
		if err := srv.Reset(); err != nil {
			log.Printf("reset failed %v", err)
		}
	}()
	res := <-stopStreamChan
	if res.err != nil {
		t.Fatal(res.err)
	}
	duration := time.Since(start)
	if duration >= 1*time.Minute {
		t.Fatal("stream not stopped in less than 1 minute after `cvd reset`")
	}
}

type result struct {
	read int
	err  error
}

func stream(duration time.Duration, ch chan result) {
	ctx, cancel := context.WithTimeout(context.Background(), duration)
	defer cancel()
	url := "http://0.0.0.0:2080/cvds/cvd_1/1/logs/logcat/:stream"
	req, err := http.NewRequestWithContext(ctx, "GET", url, nil)
	if err != nil {
		ch <- result{err: err}
		return
	}
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		ch <- result{err: err}
		return
	}
	defer resp.Body.Close()
	totalRead := 0
	buffer := make([]byte, 1024)
	for {
		n, err := resp.Body.Read(buffer)
		if err != nil {
			if err == io.EOF || errors.Is(err, context.DeadlineExceeded) {
				ch <- result{read: totalRead}
			} else {
				ch <- result{err: err}
			}
			return
		}
		totalRead += n
	}
}
