// Copyright 2021 Google LLC
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
	"math/rand"
	"net/http"
	"os"
	"strconv"
	"sync"
	"time"

	"cuttlefish/host-orchestrator/orchestrator"
	apiv1 "cuttlefish/liboperator/api/v1"
	"cuttlefish/liboperator/operator"

	"github.com/gorilla/mux"
)

const (
	defaultSocketPath      = "/run/cuttlefish/operator"
	defaultHttpPort        = "1080"
	defaultHttpsPort       = "1443"
	defaultTLSCertDir      = "/etc/cuttlefish-common/host-orchestrator/cert"
	defaultAndroidBuildURL = "https://androidbuildinternal.googleapis.com"
	defaultCVDArtifactsDir = "/var/lib/cuttlefish-common"
	staticFilesDir         = "static"    // relative path
	interceptDir           = "intercept" // relative path
)

func startHttpServer() {
	httpPort := fromEnvOrDefault("ORCHESTRATOR_HTTP_PORT", defaultHttpPort)
	log.Println(fmt.Sprint("Host Orchestrator is listening at http://localhost:", httpPort))

	log.Fatal(http.ListenAndServe(
		fmt.Sprint(":", httpPort),
		// handler is nil, so DefaultServeMux is used.
		nil))
}

func startHttpsServer() {
	tlsCertDir := fromEnvOrDefault("ORCHESTRATOR_TLS_CERT_DIR", defaultTLSCertDir)
	httpsPort := fromEnvOrDefault("ORCHESTRATOR_HTTPS_PORT", defaultHttpsPort)
	certPath := tlsCertDir + "/cert.pem"
	keyPath := tlsCertDir + "/key.pem"
	log.Println(fmt.Sprint("Host Orchestrator is listening at https://localhost:", httpsPort))
	log.Fatal(http.ListenAndServeTLS(fmt.Sprint(":", httpsPort),
		certPath,
		keyPath,
		// handler is nil, so DefaultServeMux is used.
		//
		// Using DefaultServerMux in both servers (http and https) is not a problem
		// as http.ServeMux instances are thread safe.
		nil))
}

func fromEnvOrDefault(key string, def string) string {
	val := os.Getenv(key)
	if val == "" {
		return def
	}
	return val
}

func fromEnvOrDefaultBool(key string, def bool) bool {
	val := os.Getenv(key)
	if val == "" {
		return def
	}
	b, err := strconv.ParseBool(val)
	if err != nil {
		panic(err)
	}
	return b
}

// Whether a device file request should be intercepted and served from the signaling server instead
func maybeIntercept(path string) *string {
	if path == "/js/server_connector.js" {
		alt := fmt.Sprintf("%s%s", interceptDir, path)
		return &alt
	}
	return nil
}

func main() {
	socketPath := fromEnvOrDefault("ORCHESTRATOR_SOCKET_PATH", defaultSocketPath)
	rand.Seed(time.Now().UnixNano())
	pool := operator.NewDevicePool()
	polledSet := operator.NewPolledSet()
	config := apiv1.InfraConfig{
		Type: "config",
		IceServers: []apiv1.IceServer{
			apiv1.IceServer{URLs: []string{"stun:stun.l.google.com:19302"}},
		},
	}
	abURL := fromEnvOrDefault("ORCHESTRATOR_ANDROID_BUILD_URL", defaultAndroidBuildURL)
	cvdArtifactsDir := fromEnvOrDefault("ORCHESTRATOR_CVD_ARTIFACTS_DIR", defaultCVDArtifactsDir)
	artifactDownloader := orchestrator.NewSignedURLArtifactDownloader(http.DefaultClient, abURL)
	cvdHandler := orchestrator.NewCVDHandler(cvdArtifactsDir, artifactDownloader)
	om := orchestrator.NewMapOM()
	im := orchestrator.NewInstanceManager(cvdHandler, om)

	operator.SetupDeviceEndpoint(pool, config, socketPath)
	r := mux.NewRouter()
	operator.SetupWebSocketEndpoint(r, pool, config)
	operator.SetupHttpEndpoints(r, pool, polledSet, config, maybeIntercept)
	orchestrator.SetupInstanceManagement(r, im, om)
	fs := http.FileServer(http.Dir(staticFilesDir))
	r.PathPrefix("/").Handler(fs)
	http.Handle("/", r)

	starters := []func(){startHttpServer, startHttpsServer}
	wg := new(sync.WaitGroup)
	wg.Add(len(starters))
	for _, starter := range starters {
		go func(f func()) {
			defer wg.Done()
			f()
		}(starter)
	}
	wg.Wait()
}
