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
	"flag"
	"fmt"
	"log"
	"net/http"
	"net/http/httputil"
	"net/url"
	"os"
	"sync"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/liboperator/api/v1"
	"github.com/google/android-cuttlefish/frontend/src/liboperator/operator"
)

const (
	DefaultSocketPath        = "/run/cuttlefish/operator"
	DefaultControlSocketPath = "/run/cuttlefish/operator_control"
	DefaultHttpPort          = 1080
	DefaultTLSCertDir        = "/etc/cuttlefish-common/operator/cert"
	DefaultStaticFilesDir    = "static"    // relative path
	DefaultInterceptDir      = "intercept" // relative path
	DefaultWebUIUrl          = ""
	DefaultListenAddress     = "127.0.0.1"
)

func startHttpServer(address string, port int) error {
	log.Println(fmt.Sprint("Operator is listening at http://localhost:", port))

	// handler is nil, so DefaultServeMux is used.
	return http.ListenAndServe(fmt.Sprintf("%s:%d", address, port), nil)
}

func startHttpsServer(address string, port int, certPath string, keyPath string) error {
	log.Println(fmt.Sprint("Operator is listening at https://localhost:", port))
	return http.ListenAndServeTLS(fmt.Sprintf("%s:%d", address, port),
		certPath,
		keyPath,
		// handler is nil, so DefaultServeMux is used.
		//
		// Using DefaultServerMux in both servers (http and https) is not a problem
		// as http.ServeMux instances are thread safe.
		nil)
}

func fromEnvOrDefault(key string, def string) string {
	val := os.Getenv(key)
	if val == "" {
		return def
	}
	return val
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
	socketPath := flag.String("socket_path", DefaultSocketPath, "Path to the device endpoint unix socket.")
	controlSocketPath := flag.String("control_socket_path", DefaultControlSocketPath, "Path to the control endpoint unix socket.")
	httpPort := flag.Int("http_port", DefaultHttpPort, "Port to serve HTTP requests on.")
	httpsPort := flag.Int("https_port", -1, "Port to serve HTTPS requests on.")
	tlsCertDir := flag.String("tls_cert_dir", DefaultTLSCertDir, "Directory where the TLS certificates are located.")
	webUiUrlStr := flag.String("webui_url", DefaultWebUIUrl, "WebUI URL.")
	address := flag.String("listen_addr", DefaultListenAddress, "IP address to listen for requests.")

	flag.Parse()

	certPath := *tlsCertDir + "/cert.pem"
	keyPath := *tlsCertDir + "/key.pem"

	pool := operator.NewDevicePool()
	polledSet := operator.NewPolledSet()
	config := apiv1.InfraConfig{
		Type: "config",
		IceServers: []apiv1.IceServer{
			{URLs: []string{"stun:stun.l.google.com:19302"}},
		},
	}

	r := operator.CreateHttpHandlers(pool, polledSet, config, maybeIntercept)
	if *webUiUrlStr != "" {
		webUiUrl, _ := url.Parse(*webUiUrlStr)
		proxy := httputil.NewSingleHostReverseProxy(webUiUrl)
		r.PathPrefix("/").Handler(proxy)
	} else {
		fs := http.FileServer(http.Dir(DefaultStaticFilesDir))
		r.PathPrefix("/").Handler(fs)
	}
	http.Handle("/", r)

	starters := []func() error{
		func() error {
			st, err := operator.SetupControlEndpoint(pool, *controlSocketPath)
			if err != nil {
				return err
			}
			return st()
		},
		func() error {
			st, err := operator.SetupDeviceEndpoint(pool, config, *socketPath)
			if err != nil {
				return err
			}
			return st()
		},
		func() error { return startHttpServer(*address, *httpPort) },
	}
	if *httpsPort > 0 {
		starters = append(starters, func() error {
			return startHttpsServer(*address, *httpsPort, certPath, keyPath)
		})
	}
	start(starters)
}
