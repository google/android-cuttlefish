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

// cvd-server-bootstrapper is a companion tool for the host orchestrator
// for bootstrapping AOSP cvd server.
package main

import (
	"log"
	"net"
	"net/http"
	"net/rpc"
	"os"
)

type Service struct{}

func (s *Service) Bootstrap(_ int, reply *string) error {
	*reply = "not implemented"
	return nil
}

func getRequiredEnvVar(key string) string {
	val, ok := os.LookupEnv(key)
	if !ok {
		log.Fatalf("missing env variable: %q", key)
	}
	return val
}

func main() {
	sockAddr := getRequiredEnvVar("RPC_SOCK_ADDR")
	if err := os.RemoveAll(sockAddr); err != nil {
		log.Fatal(err)
	}
	l, e := net.Listen("unix", sockAddr)
	if e != nil {
		log.Fatal("listen error:", e)
	}
	// Make sure the socket is only accessible by owner and group
	if err := os.Chmod(sockAddr, 0770); err != nil {
		log.Fatal(err)
	}
	srv := &Service{}
	rpc.Register(srv)
	rpc.HandleHTTP()
	log.Println("serving...")
	http.Serve(l, nil)
}
