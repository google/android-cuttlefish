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
	"flag"
	"fmt"
	"log"
	"net/http"
	"net/http/httputil"
	"net/url"
	"os"
	"path/filepath"
	"sync"
	"time"

	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/debug"
	"github.com/gorilla/mux"

	"github.com/google/uuid"
)

const (
	defaultTLSCertDir               = "/etc/cuttlefish-common/host_orchestrator/cert"
	defaultAndroidBuildURL          = "https://androidbuildinternal.googleapis.com"
	defaultCVDBinAndroidBuildID     = "10796991"
	defaultCVDBinAndroidBuildTarget = "aosp_cf_x86_64_phone-trunk_staging-userdebug"
	defaultCVDArtifactsDir          = "/var/lib/cuttlefish-common"
)

func startHttpServer(port int) error {
	log.Println(fmt.Sprintf("Host Orchestrator is listening at http://localhost:%d", port))

	// handler is nil, so DefaultServeMux is used.
	return http.ListenAndServe(fmt.Sprintf(":%d", port), nil)
}

func startHttpsServer(port int, certPath string, keyPath string) error {
	log.Println(fmt.Sprint("Host Orchestrator is listening at https://localhost:", port))
	return http.ListenAndServeTLS(fmt.Sprintf(":%d", port),
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
	httpPort := flag.Int("http_port", 1080, "Port to listen on for HTTP requests.")
	httpsPort := flag.Int("https_port", -1, "Port to listen on for HTTPS requests.")
	tlsCertDir := flag.String("tls_cert_dir", defaultTLSCertDir, "Directory with the TLS certificate.")
	cvdUser := flag.String("cvd_user", "", "User to execute cvd as.")
	operatorPort := flag.Int("operator_http_port", 2080, "Port where the operator is listening.")
	abURL := flag.String("android_build_url", defaultAndroidBuildURL, "URL to an Android Build API.")
	cvdBinAndroidBuildID := flag.String("cvd_build_id", defaultCVDBinAndroidBuildID, "Build ID to fetch the cvd binary from.")
	cvdBinAndroidBuildTarget := flag.String("cvd_build_target", defaultCVDBinAndroidBuildTarget, "Build target to fetch the cvd binary from.")
	imRootDir := flag.String("cvd_artifacts_dir", defaultCVDArtifactsDir, "Directory where cvd will download android build artifacts to.")

	flag.Parse()

	certPath := filepath.Join(*tlsCertDir, "cert.pem")
	keyPath := filepath.Join(*tlsCertDir, "key.pem")

	imPaths := orchestrator.IMPaths{
		RootDir:          *imRootDir,
		CVDToolsDir:      *imRootDir,
		ArtifactsRootDir: filepath.Join(*imRootDir, "artifacts"),
		RuntimesRootDir:  filepath.Join(*imRootDir, "runtimes"),
	}
	om := orchestrator.NewMapOM()
	uamOpts := orchestrator.UserArtifactsManagerOpts{
		RootDir:     filepath.Join(*imRootDir, "user_artifacs"),
		NameFactory: func() string { return uuid.New().String() },
	}
	uam := orchestrator.NewUserArtifactsManagerImpl(uamOpts)
	cvdToolsVersion := orchestrator.AndroidBuild{
		ID:     *cvdBinAndroidBuildID,
		Target: *cvdBinAndroidBuildTarget,
	}
	debugStaticVars := debug.StaticVariables{
		InitialCVDBinAndroidBuildID:     cvdToolsVersion.ID,
		InitialCVDBinAndroidBuildTarget: cvdToolsVersion.Target,
	}
	debugVarsManager := debug.NewVariablesManager(debugStaticVars)
	imController := orchestrator.Controller{
		Config: orchestrator.Config{
			Paths:                  imPaths,
			CVDToolsVersion:        cvdToolsVersion,
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
		func() error { return startHttpServer(*httpPort) },
	}
	if *httpsPort > 0 {
		starters = append(starters, func() error { return startHttpsServer(*httpsPort, certPath, keyPath) })
	}
	start(starters)
}
