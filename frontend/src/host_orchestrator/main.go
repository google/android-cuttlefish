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
	"os/exec"
	"path/filepath"
	"sync"
	"time"

	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/cvd"
	"github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/debug"
	apiv1 "github.com/google/android-cuttlefish/frontend/src/liboperator/api/v1"
	"github.com/google/android-cuttlefish/frontend/src/liboperator/operator"

	"github.com/google/uuid"
)

const (
	DefaultSocketPath     = "/run/cuttlefish/operator"
	DefaultHttpPort       = "1080"
	DefaultHttpsPort      = "1443"
	DefaultTLSCertDir     = "/etc/cuttlefish-common/host_orchestrator/cert"
	DefaultStaticFilesDir = "static"    // relative path
	DefaultInterceptDir   = "intercept" // relative path
	DefaultWebUIUrl       = ""

	defaultAndroidBuildURL          = "https://androidbuildinternal.googleapis.com"
	defaultCVDBinAndroidBuildID     = "10234205"
	defaultCVDBinAndroidBuildTarget = "aosp_cf_x86_64_phone-userdebug"
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

// Whether a device file request should be intercepted and served from the signaling server instead
func maybeIntercept(path string) *string {
	if path == "/js/server_connector.js" {
		alt := fmt.Sprintf("%s%s", DefaultInterceptDir, path)
		return &alt
	}
	return nil
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

func main() {
	socketPath := fromEnvOrDefault("ORCHESTRATOR_SOCKET_PATH", DefaultSocketPath)
	httpPort := fromEnvOrDefault("ORCHESTRATOR_HTTP_PORT", DefaultHttpPort)
	httpsPort := fromEnvOrDefault("ORCHESTRATOR_HTTPS_PORT", DefaultHttpsPort)
	tlsCertDir := fromEnvOrDefault("ORCHESTRATOR_TLS_CERT_DIR", DefaultTLSCertDir)
	webUIUrlStr := fromEnvOrDefault("ORCHESTRATOR_WEBUI_URL", DefaultWebUIUrl)
	certPath := filepath.Join(tlsCertDir, "cert.pem")
	keyPath := filepath.Join(tlsCertDir, "key.pem")

	pool := operator.NewDevicePool()
	polledSet := operator.NewPolledSet()
	config := apiv1.InfraConfig{
		Type: "config",
		IceServers: []apiv1.IceServer{
			{URLs: []string{"stun:stun.l.google.com:19302"}},
		},
	}
	abURL := fromEnvOrDefault("ORCHESTRATOR_ANDROID_BUILD_URL", defaultAndroidBuildURL)
	cvdBinAndroidBuildID := fromEnvOrDefault("ORCHESTRATOR_CVDBIN_ANDROID_BUILD_ID", defaultCVDBinAndroidBuildID)
	cvdBinAndroidBuildTarget := fromEnvOrDefault("ORCHESTRATOR_CVDBIN_ANDROID_BUILD_TARGET", defaultCVDBinAndroidBuildTarget)
	imRootDir := fromEnvOrDefault("ORCHESTRATOR_CVD_ARTIFACTS_DIR", defaultCVDArtifactsDir)
	runtimesRootDir := filepath.Join(imRootDir, "runtimes")
	imPaths := orchestrator.IMPaths{
		RootDir:          imRootDir,
		CVDToolsDir:      imRootDir,
		ArtifactsRootDir: filepath.Join(imRootDir, "artifacts"),
		RuntimesRootDir:  runtimesRootDir,
	}
	om := orchestrator.NewMapOM()
	uamOpts := orchestrator.UserArtifactsManagerOpts{
		RootDir:     filepath.Join(imRootDir, "user_artifacs"),
		NameFactory: func() string { return uuid.New().String() },
	}
	uam := orchestrator.NewUserArtifactsManagerImpl(uamOpts)
	opts := orchestrator.CVDToolInstanceManagerOpts{
		ExecContext: exec.CommandContext,
		CVDToolsVersion: orchestrator.AndroidBuild{
			ID:     cvdBinAndroidBuildID,
			Target: cvdBinAndroidBuildTarget,
		},
		Paths:                    imPaths,
		OperationManager:         om,
		UserArtifactsDirResolver: uam,
		CVDStartTimeout:          3 * time.Minute,
		HostValidator:            &orchestrator.HostValidator{ExecContext: exec.CommandContext},
		BuildAPIFactory: func(credentials string) orchestrator.BuildAPI {
			return orchestrator.NewAndroidCIBuildAPI(http.DefaultClient, abURL, credentials)
		},
		UUIDGen: func() string { return uuid.New().String() },
	}
	im := orchestrator.NewCVDToolInstanceManager(&opts)
	debugStaticVars := debug.StaticVariables{
		InitialCVDBinAndroidBuildID:     opts.CVDToolsVersion.ID,
		InitialCVDBinAndroidBuildTarget: opts.CVDToolsVersion.Target,
	}
	debugVarsManager := debug.NewVariablesManager(debugStaticVars)
	deviceServerLoop := operator.SetupDeviceEndpoint(pool, config, socketPath)
	go func() {
		err := deviceServerLoop()
		log.Fatal("Error with device endpoint: ", err)
	}()
	r := operator.CreateHttpHandlers(pool, polledSet, config, maybeIntercept)
	imController := orchestrator.Controller{
		InstanceManager:         im,
		OperationManager:        om,
		WaitOperationDuration:   2 * time.Minute,
		UserArtifactsManager:    uam,
		DebugVariablesManager:   debugVarsManager,
		RuntimeArtifactsManager: cvd.NewRuntimeArtifactsManager(runtimesRootDir),
	}
	imController.AddRoutes(r)
	// The host orchestrator currently has no use for this, since clients won't connect
	// to it directly, however they probably will once the multi-device feature matures.
	if len(webUIUrlStr) > 0 {
		webUIUrl, _ := url.Parse(webUIUrlStr)
		proxy := httputil.NewSingleHostReverseProxy(webUIUrl)
		r.PathPrefix("/").Handler(proxy)
	} else {
		fs := http.FileServer(http.Dir(DefaultStaticFilesDir))
		r.PathPrefix("/").Handler(fs)
	}
	http.Handle("/", r)

	starters := []func() error{
		func() error { return operator.SetupDeviceEndpoint(pool, config, socketPath)() },
		func() error { return startHttpsServer(httpsPort, certPath, keyPath) },
		func() error { return startHttpServer(httpPort) },
	}
	start(starters)
}
