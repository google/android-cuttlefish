// Copyright 2025 Google LLC
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

package libhoclient

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"time"

	hoapi "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	wclient "github.com/google/android-cuttlefish/frontend/src/libhoclient/webrtcclient"
	"github.com/gorilla/websocket"
)

// Fake implementation of the HostOrchestratorClient interface for use in testing
type FakeHostOrchestratorClient struct {
	cvds       map[int]*hoapi.CVD
	operations map[string]chan []byte
	imageDirs  []string
}

func NewFakeHostOrchestratorClient() *FakeHostOrchestratorClient {
	return &FakeHostOrchestratorClient{
		cvds:       make(map[int]*hoapi.CVD),
		operations: make(map[string]chan []byte),
		imageDirs:  []string{},
	}
}

func (c *FakeHostOrchestratorClient) ConnectWebRTC(device string, observer wclient.Observer, logger io.Writer, opts ConnectWebRTCOpts) (*wclient.Connection, error) {
	return nil, nil
}

func (c *FakeHostOrchestratorClient) ConnectADBWebSocket(device string) (*websocket.Conn, error) {
	return nil, nil
}

func (c *FakeHostOrchestratorClient) FetchArtifacts(req *hoapi.FetchArtifactsRequest, creds BuildAPICreds) (*hoapi.FetchArtifactsResponse, error) {
	return &hoapi.FetchArtifactsResponse{AndroidCIBundle: req.AndroidCIBundle}, nil
}

func (c *FakeHostOrchestratorClient) CreateCVD(req *hoapi.CreateCVDRequest, creds BuildAPICreds) (*hoapi.CreateCVDResponse, error) {
	var cvds []*hoapi.CVD
	var err error
	if req.CVD != nil {
		cvds, err = c.createFakeCVDs(int(req.AdditionalInstancesNum) + 1)
	} else {
		cvds, err = c.createFakeEnvironment(req.EnvConfig)
	}
	return &hoapi.CreateCVDResponse{CVDs: cvds}, err
}

func (c *FakeHostOrchestratorClient) CreateCVDOp(req *hoapi.CreateCVDRequest, creds BuildAPICreds) (*hoapi.Operation, error) {
	op, ch := c.newFakeOperation()
	go func() {
		res, err := c.CreateCVD(req, creds)
		var msg []byte
		if err != nil {
			msg, _ = json.Marshal(err)
		} else {
			msg, _ = json.Marshal(res)
		}
		ch <- msg
	}()
	return op, nil
}

func (c *FakeHostOrchestratorClient) DeleteCVD(id string) error {
	for k, cvd := range c.cvds {
		if cvd.Group == id {
			delete(c.cvds, k)
		}
	}
	return nil
}

func (c *FakeHostOrchestratorClient) ListCVDs() ([]*hoapi.CVD, error) {
	ret := []*hoapi.CVD{}
	for _, cvd := range c.cvds {
		ret = append(ret, cvd)
	}
	return ret, nil
}

func (c *FakeHostOrchestratorClient) UploadArtifact(filename string) error {
	return nil
}

func (c *FakeHostOrchestratorClient) ExtractArtifact(filename string) (*hoapi.Operation, error) {
	op, ch := c.newFakeOperation()
	go func() {
		ch <- []byte("{}")
	}()
	return op, nil
}

func (c *FakeHostOrchestratorClient) CreateImageDirectory() (*hoapi.Operation, error) {
	op, ch := c.newFakeOperation()
	go func() {
		dir := c.randomString()
		c.imageDirs = append(c.imageDirs, dir)
		msg, _ := json.Marshal(&hoapi.CreateImageDirectoryResponse{
			ID: dir,
		})
		ch <- msg
	}()
	return op, nil
}

func (c *FakeHostOrchestratorClient) ListImageDirectories() (*hoapi.ListImageDirectoriesResponse, error) {
	imageDirs := []hoapi.ImageDirectory{}
	for _, dir := range c.imageDirs {
		imageDirs = append(imageDirs, hoapi.ImageDirectory{ID: dir})
	}
	return &hoapi.ListImageDirectoriesResponse{
		ImageDirs: imageDirs,
	}, nil
}

func (c *FakeHostOrchestratorClient) UpdateImageDirectoryWithUserArtifact(id, filename string) (*hoapi.Operation, error) {
	op, ch := c.newFakeOperation()
	go func() {
		ch <- []byte("{}")
	}()
	return op, nil
}

func (c *FakeHostOrchestratorClient) DeleteImageDirectory(id string) (*hoapi.Operation, error) {
	op, ch := c.newFakeOperation()
	go func() {
		newImageDirs := []string{}
		for _, dir := range c.imageDirs {
			if dir != id {
				newImageDirs = append(newImageDirs, dir)
			}
		}
		c.imageDirs = newImageDirs
		ch <- []byte("{}")
	}()
	return op, nil
}

func (c *FakeHostOrchestratorClient) WaitForOperation(opName string, result any) error {
	ch, ok := c.operations[opName]
	if !ok {
		return fmt.Errorf("operation not found")
	}
	if ch == nil {
		return fmt.Errorf("operation already waited for")
	}
	res := <-ch
	if result != nil {
		decoder := json.NewDecoder(bytes.NewReader(res))
		return decoder.Decode(result)
	}
	return nil
}

func (c *FakeHostOrchestratorClient) CreateBugReport(group string, opts CreateBugReportOpts, dst io.Writer) error {
	dst.Write([]byte("bugreport"))
	return nil
}

func (c *FakeHostOrchestratorClient) Powerwash(groupName, instanceName string) error { return nil }

func (c *FakeHostOrchestratorClient) Stop(groupName, instanceName string) error { return nil }

func (c *FakeHostOrchestratorClient) Start(groupName, instanceName string, req *hoapi.StartCVDRequest) error {
	return nil
}

func (c *FakeHostOrchestratorClient) CreateSnapshot(groupName, instanceName string, req *hoapi.CreateSnapshotRequest) (*hoapi.CreateSnapshotResponse, error) {
	return &hoapi.CreateSnapshotResponse{SnapshotID: c.randomString()}, nil
}

func (c *FakeHostOrchestratorClient) Powerbtn(groupName, instanceName string) error {
	return nil
}

func (c *FakeHostOrchestratorClient) DeleteSnapshot(string) error {
	return nil
}

func (c *FakeHostOrchestratorClient) CreateUploadDir() (string, error) {
	return c.randomString(), nil
}

func (c *FakeHostOrchestratorClient) UploadFile(uploadDir string, filename string) error {
	return nil
}

func (c *FakeHostOrchestratorClient) UploadFileWithOptions(uploadDir string, filename string, options UploadOptions) error {
	return nil
}

func (c *FakeHostOrchestratorClient) ExtractFile(uploadDir string, filename string) (*hoapi.Operation, error) {
	op, ch := c.newFakeOperation()
	go func() { ch <- []byte("{}") }()
	return op, nil
}

func (c *FakeHostOrchestratorClient) createFakeCVDs(total int) ([]*hoapi.CVD, error) {
	cvds := []*hoapi.CVD{}
	for cnt := 0; cnt < total; cnt++ {
		id := c.unusedCVDId()
		cvd := &hoapi.CVD{
			Group:          fmt.Sprintf("cvd-%d", id),
			Name:           fmt.Sprintf("%d", id),
			Status:         "Running",
			Displays:       []string{"720 x 1280 (320)"},
			WebRTCDeviceID: fmt.Sprintf("cvd-%d-%d", id, id),
			ADBSerial:      fmt.Sprintf("0.0.0.0:%d", 6520+id-1),
		}
		c.cvds[id] = cvd
		cvds = append(cvds, cvd)
	}
	return cvds, nil
}

func (c *FakeHostOrchestratorClient) createFakeEnvironment(config map[string]interface{}) ([]*hoapi.CVD, error) {
	instancesAny, hasInstances := config["instances"]
	if !hasInstances {
		return nil, fmt.Errorf("no instances in config")
	}
	instances, instancesIsArray := instancesAny.([]interface{})
	if !instancesIsArray {
		return nil, fmt.Errorf("instances is not an array")
	}
	cvds, _ := c.createFakeCVDs(len(instances))
	// Use the default one if not provided in the config
	groupName := cvds[0].Group
	if commonAny, hasCommon := config["common"]; hasCommon {
		if common, commonIsMap := commonAny.(map[string]interface{}); commonIsMap {
			if nameAny, hasName := common["group_name"]; hasName {
				if name, nameIsString := nameAny.(string); nameIsString {
					groupName = name
				}
			}
		}
	}

	for i := 0; i < len(cvds); i++ {
		cvds[i].Group = groupName
		if instance, instanceIsMap := instances[i].(map[string]interface{}); instanceIsMap {
			if nameAny, hasName := instance["name"]; hasName {
				if name, nameIsStr := nameAny.(string); nameIsStr {
					cvds[i].Name = name
				}
			}
		}
	}

	return cvds, nil
}

func (c *FakeHostOrchestratorClient) unusedCVDId() int {
	id := 1
	for {
		if _, inUse := c.cvds[id]; !inUse {
			return id
		}
		id++
	}
}

func (c *FakeHostOrchestratorClient) newFakeOperation() (*hoapi.Operation, chan []byte) {
	ch := make(chan []byte)
	op := &hoapi.Operation{
		Name: fmt.Sprintf("%d", len(c.operations)),
		Done: false,
	}
	c.operations[op.Name] = ch
	return op, ch
}

func (c *FakeHostOrchestratorClient) randomString() string {
	return fmt.Sprint(time.Now().UnixNano())
}
