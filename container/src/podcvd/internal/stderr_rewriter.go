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

package internal

import (
	"bytes"
	"io"
	"strings"
)

// StderrRewriter intercepts container stderr in real time and rewrites
// cvd-specific outputs (such as `cvd logs` and internal WebRTC addresses)
// into podcvd-compatible guidance for host users.
type StderrRewriter struct {
	out io.Writer
	ip  string
	buf []byte
}

func NewStderrRewriter(out io.Writer, ip string) *StderrRewriter {
	if out == nil {
		out = io.Discard
	}
	return &StderrRewriter{
		out: out,
		ip:  ip,
	}
}

func (r *StderrRewriter) Write(p []byte) (int, error) {
	r.buf = append(r.buf, p...)
	idx := bytes.LastIndexByte(r.buf, '\n')
	if idx < 0 {
		return len(p), nil
	}
	if _, err := io.WriteString(r.out, r.rewrite(string(r.buf[:idx+1]))); err != nil {
		return 0, err
	}
	r.buf = r.buf[idx+1:]
	return len(p), nil
}

func (r *StderrRewriter) Flush() error {
	if len(r.buf) > 0 {
		_, err := io.WriteString(r.out, r.rewrite(string(r.buf)))
		r.buf = nil
		return err
	}
	return nil
}

func (r *StderrRewriter) rewrite(s string) string {
	s = strings.ReplaceAll(s, "`cvd logs`", "`podcvd logs`")
	if r.ip != "" {
		s = updateIPAndPortString(s, r.ip)
	}
	return s
}
