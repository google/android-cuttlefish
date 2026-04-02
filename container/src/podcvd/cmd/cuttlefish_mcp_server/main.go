// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//	http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package main

import (
	"context"
	"fmt"
	"log"

	"github.com/google/android-cuttlefish/container/src/podcvd/internal"
	"github.com/modelcontextprotocol/go-sdk/mcp"
)

type toolHandler struct {
	// TODO(seungjaeyoo): Add client ID to distinguish AI agents.
}

type groupNameArg struct {
	GroupName string `json:"group_name,omitempty"`
}

type createArgs struct {
	groupNameArg
}

func (h *toolHandler) Create(ctx context.Context, req *mcp.CallToolRequest, args createArgs) (*mcp.CallToolResult, any, error) {
	cmd := []string{"create", "--vhost_user_vsock=true", "--report_anonymous_usage_stats=n"}
	if args.GroupName != "" {
		cmd = append([]string{fmt.Sprintf("--group_name=%s", args.GroupName)}, cmd...)
	}
	if err := internal.Main(cmd); err != nil {
		return nil, nil, err
	}
	return &mcp.CallToolResult{
		Content: []mcp.Content{
			&mcp.TextContent{Text: "Successfully created Cuttlefish instance group"},
		},
	}, nil, nil
}

func main() {
	handlers := &toolHandler{}
	server := mcp.NewServer(&mcp.Implementation{Name: "cuttlefish-mcp-server", Version: "v0.0.1"}, nil)
	mcp.AddTool(server, &mcp.Tool{
		Name:        "create",
		Description: "Create a Cuttlefish instance group",
	}, handlers.Create)

	if err := server.Run(context.Background(), &mcp.StdioTransport{}); err != nil {
		log.Fatalf("Server failed: %v", err)
	}
}
