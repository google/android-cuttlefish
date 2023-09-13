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
	DefaultHttpPort                 = "1080"
	DefaultTLSCertDir               = "/etc/cuttlefish-common/host_orchestrator/cert"
	defaultAndroidBuildURL          = "https://androidbuildinternal.googleapis.com"
	defaultCVDBinAndroidBuildID     = "10796991"
	defaultCVDBinAndroidBuildTarget = "aosp_cf_x86_64_phone-trunk_staging-userdebug"
	defaultCVDArtifactsDir          = "/var/lib/cuttlefish-common"
)

func startHttpServer(port string) error {
	log.Println(fmt.Sprint("Host Orchestrator is listening at http://localhost:", port))

	// handler is nil, so DefaultServeMux is used.
	return http.ListenAndServe(fmt.Sprint(":", port), nil)
}

func startHttpsServer(port string, certPath string, keyPath string) error {
	log.Println(fmt.Sprint("Host Orchestrator is listening at https://localhost:", port))
	return http.ListenAndServeTLS(fmt.Sprint(":", port),
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

func newOperatorProxy(port string) *httputil.ReverseProxy {
	if port == "" {
		log.Fatal("The host orchestrator requires access to the operator")
	}
	operatorURL, err := url.Parse(fmt.Sprintf("http://127.0.0.1:%s", port))
	if err != nil {
		log.Fatalf("Invalid operator port (%s): %v", port, err)
	}
	return httputil.NewSingleHostReverseProxy(operatorURL)
}

func main() {
	httpPort := fromEnvOrDefault("ORCHESTRATOR_HTTP_PORT", DefaultHttpPort)
	httpsPort := fromEnvOrDefault("ORCHESTRATOR_HTTPS_PORT", "")
	tlsCertDir := fromEnvOrDefault("ORCHESTRATOR_TLS_CERT_DIR", DefaultTLSCertDir)
	certPath := filepath.Join(tlsCertDir, "cert.pem")
	keyPath := filepath.Join(tlsCertDir, "key.pem")
	cvdUser := fromEnvOrDefault("ORCHESTRATOR_CVD_USER", "")
	operatorPort := fromEnvOrDefault("OPERATOR_HTTP_PORT", "")

	abURL := fromEnvOrDefault("ORCHESTRATOR_ANDROID_BUILD_URL", defaultAndroidBuildURL)
	cvdBinAndroidBuildID := fromEnvOrDefault("ORCHESTRATOR_CVDBIN_ANDROID_BUILD_ID", defaultCVDBinAndroidBuildID)
	cvdBinAndroidBuildTarget := fromEnvOrDefault("ORCHESTRATOR_CVDBIN_ANDROID_BUILD_TARGET", defaultCVDBinAndroidBuildTarget)
	imRootDir := fromEnvOrDefault("ORCHESTRATOR_CVD_ARTIFACTS_DIR", defaultCVDArtifactsDir)
	imPaths := orchestrator.IMPaths{
		RootDir:          imRootDir,
		CVDToolsDir:      imRootDir,
		ArtifactsRootDir: filepath.Join(imRootDir, "artifacts"),
		RuntimesRootDir:  filepath.Join(imRootDir, "runtimes"),
	}
	om := orchestrator.NewMapOM()
	uamOpts := orchestrator.UserArtifactsManagerOpts{
		RootDir:     filepath.Join(imRootDir, "user_artifacs"),
		NameFactory: func() string { return uuid.New().String() },
	}
	uam := orchestrator.NewUserArtifactsManagerImpl(uamOpts)
	cvdToolsVersion := orchestrator.AndroidBuild{
		ID:     cvdBinAndroidBuildID,
		Target: cvdBinAndroidBuildTarget,
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
			AndroidBuildServiceURL: abURL,
			CVDUser:                cvdUser,
		},
		OperationManager:      om,
		WaitOperationDuration: 2 * time.Minute,
		UserArtifactsManager:  uam,
		DebugVariablesManager: debugVarsManager,
	}
	proxy := newOperatorProxy(operatorPort)

	r := mux.NewRouter()
	imController.AddRoutes(r)
	// Defer to the operator for every route not covered by the orchestrator. That
	// includes web UI and other static files.
	r.PathPrefix("/").Handler(proxy)

	http.Handle("/", r)

	starters := []func() error{
		func() error { return startHttpServer(httpPort) },
	}
	if httpsPort != "" {
		starters = append(starters, func() error { return startHttpsServer(httpsPort, certPath, keyPath) })
	}
	start(starters)
}
