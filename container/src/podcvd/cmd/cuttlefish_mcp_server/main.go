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
	"log"

	"github.com/google/android-cuttlefish/container/src/podcvd/internal"

	"github.com/modelcontextprotocol/go-sdk/mcp"
)

type CreateArgs struct{}

func main() {
	server := mcp.NewServer(&mcp.Implementation{Name: "cuttlefish-mcp-server", Version: "v0.0.1"}, nil)

	mcp.AddTool(server, &mcp.Tool{
		Name:        "create",
		Description: "Create a Cuttlefish instance group within the container instance",
	}, func(ctx context.Context, req *mcp.CallToolRequest, args CreateArgs) (*mcp.CallToolResult, any, error) {
		internal.Main([]string{"create", "--vhost_user_vsock=true", "--report_anonymous_usage_stats=n"})

		return &mcp.CallToolResult{
			Content: []mcp.Content{
				&mcp.TextContent{Text: "created a Cuttlefish instance group successfully"},
			},
		}, nil, nil
	})

	if err := server.Run(context.Background(), &mcp.StdioTransport{}); err != nil {
		log.Fatalf("Server failed: %v", err)
	}
}
