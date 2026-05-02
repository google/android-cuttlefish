// Copyright 2026 Google LLC
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

package orchestrator

import (
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"os/exec"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
	"github.com/google/uuid"
	"github.com/modelcontextprotocol/go-sdk/mcp"
)

type ListCVDsParams struct {
	Group string `json:"group,omitempty"`
}

func NewMCPHandler(config Config, om OperationManager, uam UserArtifactsManager) http.Handler {
	server := mcp.NewServer(&mcp.Implementation{Name: "host-orchestrator", Version: "1.0"}, &mcp.ServerOptions{
		InitializedHandler: func(context.Context, *mcp.InitializedRequest) {
			fmt.Println("initialized!")
		},
		GetSessionID: func() string {
			return ""
		},
	})

	mcp.AddTool(server, &mcp.Tool{
		Name:        "list_cvds",
		Description: "List all cuttlefish instances",
	}, func(ctx context.Context, req *mcp.CallToolRequest, args ListCVDsParams) (*mcp.CallToolResult, any, error) {
		action := NewListCVDsAction(ListCVDsActionOpts{
			Group:       args.Group,
			Paths:       config.Paths,
			ExecContext: exec.CommandContext,
		})
		resp, err := action.Run()
		if err != nil {
			return nil, nil, err
		}
		jsonData, err := json.Marshal(resp)
		if err != nil {
			return nil, nil, err
		}
		return &mcp.CallToolResult{
			Content: []mcp.Content{
				&mcp.TextContent{Text: string(jsonData)},
			},
		}, nil, nil
	})

	mcp.AddTool(server, &mcp.Tool{
		Name:        "create_cvd",
		Description: "Create cuttlefish instances",
	}, func(ctx context.Context, mcpReq *mcp.CallToolRequest, args apiv1.CreateCVDRequest) (*mcp.CallToolResult, any, error) {
		dummyReq, _ := http.NewRequest("POST", "http://dummy", nil)
		creds := getFetchCredentials(config.BuildAPICredentials, dummyReq)
		cvdBundleFetcher := newFetchCVDCommandArtifactsFetcher(exec.CommandContext, creds, config.AndroidBuildServiceURL)
		opts := CreateCVDActionOpts{
			Request:                  &args,
			HostValidator:            &HostValidator{ExecContext: exec.CommandContext},
			Paths:                    config.Paths,
			OperationManager:         om,
			ExecContext:              exec.CommandContext,
			CVDBundleFetcher:         cvdBundleFetcher,
			UUIDGen:                  func() string { return uuid.New().String() },
			UserArtifactsDirResolver: uam,
			FetchCredentials:         creds,
			BuildAPIBaseURL:          config.AndroidBuildServiceURL,
		}
		resp, err := NewCreateCVDAction(opts).Run()
		if err != nil {
			return nil, nil, err
		}
		jsonData, err := json.Marshal(resp)
		if err != nil {
			return nil, nil, err
		}
		return &mcp.CallToolResult{
			Content: []mcp.Content{
				&mcp.TextContent{Text: string(jsonData)},
			},
		}, nil, nil
	})

	return mcp.NewStreamableHTTPHandler(func(request *http.Request) *mcp.Server {
		return server
	}, &mcp.StreamableHTTPOptions{JSONResponse: true})
}
