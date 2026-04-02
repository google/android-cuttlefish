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

type fleetArgs struct{}

type removeArgs struct {
	*groupNameArg
}

type clearArgs struct{}

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

func (h *toolHandler) Fleet(ctx context.Context, req *mcp.CallToolRequest, args fleetArgs) (*mcp.CallToolResult, any, error) {
	if err := internal.Main([]string{"fleet"}); err != nil {
		return nil, nil, err
	}
	return &mcp.CallToolResult{
		Content: []mcp.Content{
			&mcp.TextContent{Text: "Successfully listed Cuttlefish instance groups"},
		},
	}, nil, nil
}

func (h *toolHandler) Remove(ctx context.Context, req *mcp.CallToolRequest, args removeArgs) (*mcp.CallToolResult, any, error) {
	cmd := []string{"remove"}
	if args.GroupName != "" {
		cmd = append([]string{fmt.Sprintf("--group_name=%s", args.GroupName)}, cmd...)
	}
	if err := internal.Main(cmd); err != nil {
		return nil, nil, err
	}
	return &mcp.CallToolResult{
		Content: []mcp.Content{
			&mcp.TextContent{Text: "Successfully removed Cuttlefish instance group"},
		},
	}, nil, nil
}

func (h *toolHandler) Clear(ctx context.Context, req *mcp.CallToolRequest, args clearArgs) (*mcp.CallToolResult, any, error) {
	if err := internal.Main([]string{"clear"}); err != nil {
		return nil, nil, err
	}
	return &mcp.CallToolResult{
		Content: []mcp.Content{
			&mcp.TextContent{Text: "Successfully cleared all Cuttlefish instance groups"},
		},
	}, nil, nil
}

const mcpServerInstruction string = `
This MCP server manages lifecycle of Cuttlefish instances and their groups
created by this MCP server or 'podcvd' which shares common internal logic with
this MCP server. When using this MCP server, **do not confuse** with other
extensions, MCP servers, skills, or any other component for orchestrating
lifecycle of Cuttlefish instances and their groups.
`

func main() {
	handlers := &toolHandler{}
	server := mcp.NewServer(
		&mcp.Implementation{
			Name:    "cuttlefish-mcp-server",
			Version: "v0.0.1",
		},
		&mcp.ServerOptions{
			Instructions: mcpServerInstruction,
		},
	)

	mcp.AddTool(server, &mcp.Tool{
		Name:        "create",
		Description: "Create a Cuttlefish instance group and also establish ADB connections on created Cuttlefish instances",
	}, handlers.Create)
	mcp.AddTool(server, &mcp.Tool{
		Name:        "fleet",
		Description: "List all Cuttlefish instance groups created by this agent or human user",
	}, handlers.Fleet)
	mcp.AddTool(server, &mcp.Tool{
		Name:        "remove",
		Description: "Remove a Cuttlefish instance group created by this agent or human user and also destroy corresponding ADB connections",
	}, handlers.Remove)
	mcp.AddTool(server, &mcp.Tool{
		Name:        "clear",
		Description: "Clear all Cuttlefish instance groups created by this agent or human user and also destroy corresponding ADB connections",
	}, handlers.Clear)

	if err := server.Run(context.Background(), &mcp.StdioTransport{}); err != nil {
		log.Fatalf("Server failed: %v", err)
	}
}
