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

// Package exists for running host orchestrator(HO) server.
package main

import (
	"flag"
	"fmt"
	"log"
	"net/http"
	"net/http/httputil"
	"net/url"
	"os"
	"os/user"
	"path/filepath"
	"sync"
	"time"

	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/debug"
	"github.com/gorilla/mux"

	"github.com/google/uuid"
)

const (
	defaultAndroidBuildURL = "https://androidbuildinternal.googleapis.com"
	DefaultListenAddress   = "127.0.0.1"
)

func defaultCVDArtifactsDir() string {
	u, err := user.Current()
	if err != nil {
		log.Fatalf("Unable to get current user uid: %v", err)
	}
	return fmt.Sprintf("/tmp/cvd/%s/artifacts", u.Uid)
}

func startHttpServer(addr string, port int) error {
	log.Printf("Host Orchestrator is listening at http://%s:%d", addr, port)

	// handler is nil, so DefaultServeMux is used.
	return http.ListenAndServe(fmt.Sprintf("%s:%d", addr, port), nil)
}

func startHttpsServer(addr string, port int, certPath string, keyPath string) error {
	log.Printf("Host Orchestrator is listening at https://%s:%d", addr, port)
	return http.ListenAndServeTLS(fmt.Sprintf("%s:%d", addr, port),
		certPath,
		keyPath,
		// handler is nil, so DefaultServeMux is used.
		//
		// Using DefaultServerMux in both servers (http and https) is not a problem
		// as http.ServeMux instances are thread safe.
		nil)
}

func fromEnvOrDefault(key string, def string) string {
	if val, ok := os.LookupEnv(key); ok {
		return val
	}
	return def
}

func start(starters []func() error) {
	wg := new(sync.WaitGroup)
	wg.Add(len(starters))
	for _, starter := range starters {
		go func(f func() error) {
			defer wg.Done()
			if err := f(); err != nil {
				log.Fatal(err)
			}
		}(starter)
	}
	wg.Wait()
}

func newOperatorProxy(port int) *httputil.ReverseProxy {
	if port <= 0 {
		log.Fatal("The host orchestrator requires access to the operator")
	}
	operatorURL, err := url.Parse(fmt.Sprintf("http://127.0.0.1:%d", port))
	if err != nil {
		log.Fatalf("Invalid operator port (%d): %v", port, err)
	}
	return httputil.NewSingleHostReverseProxy(operatorURL)
}

func main() {
	httpPort := flag.Int("http_port", 2081, "Port to listen on for HTTP requests.")
	cvdUser := flag.String("cvd_user", "", "User to execute cvd as.")
	operatorPort := flag.Int("operator_http_port", 1080, "Port where the operator is listening.")
	abURL := flag.String("android_build_url", defaultAndroidBuildURL, "URL to an Android Build API.")
	imRootDir := flag.String("cvd_artifacts_dir", defaultCVDArtifactsDir(), "Directory where cvd will download android build artifacts to.")
	address := flag.String("listen_addr", DefaultListenAddress, "IP address to listen for requests.")

	flag.Parse()

	if err := os.MkdirAll(*imRootDir, 0774); err != nil {
		log.Fatalf("Unable to create artifacts directory: %v", err)
	}

	imPaths := orchestrator.IMPaths{
		RootDir:          *imRootDir,
		ArtifactsRootDir: filepath.Join(*imRootDir, "artifacts"),
	}
	om := orchestrator.NewMapOM()
	uamOpts := orchestrator.UserArtifactsManagerOpts{
		RootDir:     filepath.Join(*imRootDir, "user_artifacts"),
		NameFactory: func() string { return uuid.New().String() },
	}
	uam := orchestrator.NewUserArtifactsManagerImpl(uamOpts)
	debugStaticVars := debug.StaticVariables{}
	debugVarsManager := debug.NewVariablesManager(debugStaticVars)
	imController := orchestrator.Controller{
		Config: orchestrator.Config{
			Paths:                  imPaths,
			AndroidBuildServiceURL: *abURL,
			CVDUser:                *cvdUser,
		},
		OperationManager:      om,
		WaitOperationDuration: 2 * time.Minute,
		UserArtifactsManager:  uam,
		DebugVariablesManager: debugVarsManager,
	}
	proxy := newOperatorProxy(*operatorPort)

	r := mux.NewRouter()
	imController.AddRoutes(r)
	// Defer to the operator for every route not covered by the orchestrator. That
	// includes web UI and other static files.
	r.PathPrefix("/").Handler(proxy)

	http.Handle("/", r)

	starters := []func() error{
		func() error { return startHttpServer(*address, *httpPort) },
	}
	start(starters)
}
