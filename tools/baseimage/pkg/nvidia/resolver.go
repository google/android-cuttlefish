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

package nvidia

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"regexp"
	"slices"
	"strconv"
	"strings"
	"time"

	"github.com/google/android-cuttlefish/tools/baseimage/pkg/gce"
)

const (
	// Branch selectors accepted by ResolveOpts.Branch.
	BranchGCPLTS = "gcp-lts"
	BranchLTS    = "lts"

	defaultReleasesURL     = "https://docs.nvidia.com/datacenter/tesla/drivers/releases.json"
	defaultGCPConfigURL    = "https://raw.githubusercontent.com/GoogleCloudPlatform/compute-gpu-installation/main/linux/cuda_installer/config.py"
	defaultRunfileTemplate = "https://us.download.nvidia.com/tesla/%s/NVIDIA-Linux-%s-%s.run"
)

var (
	versionRe      = regexp.MustCompile(`^[0-9]+\.[0-9]+(\.[0-9]+)?$`)
	digitsRe       = regexp.MustCompile(`^[0-9]+$`)
	gcpLTSKeyRe    = regexp.MustCompile(`["']lts["']\s*:\s*\{`)
	gcpDriverVerRe = regexp.MustCompile(`["']driver["']\s*:\s*\{[^}]*["']version["']\s*:\s*["']([0-9]+\.[0-9]+(?:\.[0-9]+)?)["']`)
)

// Client resolves NVIDIA Tesla driver versions from official NVIDIA and Google
// Cloud public metadata.
type Client struct {
	HTTPClient         *http.Client
	ReleasesURL        string
	GCPConfigURL       string
	RunfileURLTemplate string // formats (version, nvidiaArch, version)
}

// NewClient constructs a Client configured with official public endpoints.
func NewClient() *Client {
	return &Client{
		HTTPClient:         &http.Client{Timeout: 30 * time.Second},
		ReleasesURL:        defaultReleasesURL,
		GCPConfigURL:       defaultGCPConfigURL,
		RunfileURLTemplate: defaultRunfileTemplate,
	}
}

// ResolveOpts specifies constraints for selecting an NVIDIA Tesla driver.
type ResolveOpts struct {
	Arch           gce.Arch
	Version        string
	Branch         string
	BranchExplicit bool
}

// Driver holds a validated NVIDIA Tesla driver selection ready for install.
type Driver struct {
	Version    string
	Branch     string
	RunfileURL string
}

// ValidateOpts checks all ResolveOpts fields and combinations without network calls.
func ValidateOpts(opts ResolveOpts) error {
	_, err := validateArchAndOpts(opts)
	return err
}

func validateArchAndOpts(opts ResolveOpts) (string, error) {
	nvArch, err := nvidiaArch(opts.Arch)
	if err != nil {
		return "", err
	}
	if opts.Version != "" && opts.BranchExplicit {
		return "", errors.New("cannot specify both -nvidia-version and -nvidia-branch")
	}
	if opts.Version != "" && !versionRe.MatchString(opts.Version) {
		return "", fmt.Errorf("invalid -nvidia-version %q: expected format like 580.196.11", opts.Version)
	}
	branch := strings.ToLower(strings.TrimSpace(opts.Branch))
	if branch != "" && branch != BranchGCPLTS && branch != BranchLTS && !digitsRe.MatchString(branch) {
		return "", fmt.Errorf("invalid -nvidia-branch %q: must be gcp-lts, lts, or a major branch number like 580 or 620", opts.Branch)
	}
	return nvArch, nil
}

type releaseBranch struct {
	Type       string        `json:"type"`
	DriverInfo []releaseInfo `json:"driver_info"`
}

type releaseInfo struct {
	ReleaseVersion string            `json:"release_version"`
	Architectures  []string          `json:"architectures"`
	RunfileURL     map[string]string `json:"runfile_url"`
}

// Resolve selects and validates the newest NVIDIA Tesla driver matching opts.
func (c *Client) Resolve(ctx context.Context, opts ResolveOpts) (*Driver, error) {
	nvArch, err := validateArchAndOpts(opts)
	if err != nil {
		return nil, err
	}
	releases, err := c.fetchReleases(ctx)
	if err != nil {
		return nil, err
	}

	if opts.Version != "" {
		return c.resolveVersion(ctx, releases, opts.Version, nvArch)
	}

	branch := strings.ToLower(strings.TrimSpace(opts.Branch))
	if branch == "" {
		branch = BranchGCPLTS
	}
	if digitsRe.MatchString(branch) {
		br, ok := releases[branch]
		if !ok {
			return nil, fmt.Errorf("NVIDIA branch %q not found in releases.json; available branches: %s",
				branch, strings.Join(sortedBranchesDesc(releases), ", "))
		}
		info, ok := latestReleaseForArch(br.DriverInfo, nvArch)
		if !ok {
			return nil, fmt.Errorf("NVIDIA branch %s has no release supporting %s", branch, nvArch)
		}
		return &Driver{
			Version:    info.ReleaseVersion,
			Branch:     branch,
			RunfileURL: info.RunfileURL[nvArch],
		}, nil
	}
	return c.resolveLTS(ctx, releases, branch, nvArch)
}

func (c *Client) resolveVersion(ctx context.Context, releases map[string]releaseBranch, version, nvArch string) (*Driver, error) {
	for _, br := range releases {
		for _, info := range br.DriverInfo {
			if info.ReleaseVersion != version {
				continue
			}
			if !supportsArch(info, nvArch) {
				return nil, fmt.Errorf("NVIDIA driver %s does not support architecture %s (supported: %s)",
					version, nvArch, strings.Join(info.Architectures, ", "))
			}
			return &Driver{
				Version:    version,
				Branch:     majorBranch(version),
				RunfileURL: info.RunfileURL[nvArch],
			}, nil
		}
	}

	tmpl := c.RunfileURLTemplate
	if tmpl == "" {
		tmpl = defaultRunfileTemplate
	}
	candidate := fmt.Sprintf(tmpl, version, nvArch, version)
	ok, err := c.headOK(ctx, candidate)
	if err != nil {
		return nil, fmt.Errorf("checking NVIDIA driver %s at %s: %v", version, candidate, err)
	}
	if !ok {
		return nil, fmt.Errorf("NVIDIA driver %s for %s not found in releases.json or at %s", version, nvArch, candidate)
	}
	return &Driver{
		Version:    version,
		Branch:     majorBranch(version),
		RunfileURL: candidate,
	}, nil
}

func (c *Client) resolveLTS(ctx context.Context, releases map[string]releaseBranch, policy, nvArch string) (*Driver, error) {
	maxMajor := 0
	if policy == BranchGCPLTS {
		gcpMajor, err := c.fetchGCPLTSBranch(ctx)
		if err != nil {
			return nil, fmt.Errorf("resolving GCP recommended LTS branch: %v (pass -nvidia-branch=lts or a major branch like 580 to bypass)", err)
		}
		maxMajor = gcpMajor
	}

	for _, b := range sortedBranchesDesc(releases) {
		if !strings.EqualFold(releases[b].Type, "lts branch") {
			continue
		}
		majorNum, err := strconv.Atoi(b)
		if err != nil || (maxMajor > 0 && majorNum > maxMajor) {
			continue
		}
		if info, ok := latestReleaseForArch(releases[b].DriverInfo, nvArch); ok {
			return &Driver{
				Version:    info.ReleaseVersion,
				Branch:     b,
				RunfileURL: info.RunfileURL[nvArch],
			}, nil
		}
	}
	return nil, fmt.Errorf("no NVIDIA %s driver supporting %s found in releases.json", policy, nvArch)
}

func (c *Client) fetchReleases(ctx context.Context) (map[string]releaseBranch, error) {
	url := c.ReleasesURL
	if url == "" {
		url = defaultReleasesURL
	}
	raw, err := c.httpGetBytes(ctx, url)
	if err != nil {
		return nil, fmt.Errorf("fetching NVIDIA releases from %s: %v", url, err)
	}
	var all map[string]json.RawMessage
	if err := json.Unmarshal(raw, &all); err != nil {
		return nil, fmt.Errorf("parsing NVIDIA releases.json: %v", err)
	}
	branches := make(map[string]releaseBranch)
	for k, v := range all {
		if !digitsRe.MatchString(k) {
			continue
		}
		var br releaseBranch
		if err := json.Unmarshal(v, &br); err != nil {
			return nil, fmt.Errorf("parsing NVIDIA branch %q: %v", k, err)
		}
		branches[k] = br
	}
	if len(branches) == 0 {
		return nil, errors.New("NVIDIA releases.json contained no driver branches")
	}
	return branches, nil
}

func (c *Client) fetchGCPLTSBranch(ctx context.Context) (int, error) {
	url := c.GCPConfigURL
	if url == "" {
		url = defaultGCPConfigURL
	}
	raw, err := c.httpGetBytes(ctx, url)
	if err != nil {
		return 0, fmt.Errorf("fetching %s: %v", url, err)
	}
	ver, err := parseGCPLTSVersion(string(raw))
	if err != nil {
		return 0, fmt.Errorf("parsing %s: %v", url, err)
	}
	return strconv.Atoi(majorBranch(ver))
}

// parseGCPLTSVersion extracts VERSION_MAP["lts"]["driver"]["version"] from
// GoogleCloudPlatform/compute-gpu-installation linux/cuda_installer/config.py.
func parseGCPLTSVersion(configPy string) (string, error) {
	loc := gcpLTSKeyRe.FindStringIndex(configPy)
	if loc == nil {
		return "", errors.New(`"lts" entry not found in VERSION_MAP`)
	}
	block, ok := extractBracedBlock(configPy[loc[1]-1:])
	if !ok {
		return "", errors.New(`unterminated "lts" block in VERSION_MAP`)
	}
	match := gcpDriverVerRe.FindStringSubmatch(block)
	if len(match) < 2 {
		return "", errors.New(`"driver.version" not found in "lts" block`)
	}
	return match[1], nil
}

func extractBracedBlock(s string) (string, bool) {
	depth := 0
	for i := 0; i < len(s); i++ {
		switch s[i] {
		case '{':
			depth++
		case '}':
			depth--
			if depth == 0 {
				return s[:i+1], true
			}
		}
	}
	return "", false
}

func (c *Client) httpClient() *http.Client {
	if c.HTTPClient != nil {
		return c.HTTPClient
	}
	return &http.Client{Timeout: 30 * time.Second}
}

func (c *Client) httpGetBytes(ctx context.Context, rawURL string) ([]byte, error) {
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, rawURL, nil)
	if err != nil {
		return nil, err
	}
	resp, err := c.httpClient().Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("HTTP %d from %s", resp.StatusCode, rawURL)
	}
	return io.ReadAll(resp.Body)
}

func (c *Client) headOK(ctx context.Context, rawURL string) (bool, error) {
	req, err := http.NewRequestWithContext(ctx, http.MethodHead, rawURL, nil)
	if err != nil {
		return false, err
	}
	resp, err := c.httpClient().Do(req)
	if err != nil {
		return false, err
	}
	resp.Body.Close()
	switch resp.StatusCode {
	case http.StatusOK:
		return true, nil
	case http.StatusNotFound, http.StatusForbidden:
		return false, nil
	default:
		return false, fmt.Errorf("unexpected HTTP %d from %s", resp.StatusCode, rawURL)
	}
}

func nvidiaArch(arch gce.Arch) (string, error) {
	switch arch {
	case gce.ArchX86:
		return "x86_64", nil
	case gce.ArchArm:
		return "aarch64", nil
	default:
		return "", fmt.Errorf("unsupported architecture: %v", arch)
	}
}

func supportsArch(info releaseInfo, nvArch string) bool {
	if info.RunfileURL[nvArch] == "" {
		return false
	}
	return len(info.Architectures) == 0 || slices.Contains(info.Architectures, nvArch)
}

func latestReleaseForArch(infos []releaseInfo, nvArch string) (releaseInfo, bool) {
	var best releaseInfo
	found := false
	for _, info := range infos {
		if !supportsArch(info, nvArch) {
			continue
		}
		if !found || compareVersion(info.ReleaseVersion, best.ReleaseVersion) > 0 {
			best = info
			found = true
		}
	}
	return best, found
}

func sortedBranchesDesc(releases map[string]releaseBranch) []string {
	keys := make([]string, 0, len(releases))
	for k := range releases {
		keys = append(keys, k)
	}
	slices.SortFunc(keys, func(a, b string) int {
		ai, _ := strconv.Atoi(a)
		bi, _ := strconv.Atoi(b)
		return bi - ai
	})
	return keys
}

func majorBranch(ver string) string {
	before, _, _ := strings.Cut(ver, ".")
	return before
}

func compareVersion(a, b string) int {
	ap, bp := strings.Split(a, "."), strings.Split(b, ".")
	for i := 0; i < max(len(ap), len(bp)); i++ {
		var ai, bi int
		if i < len(ap) {
			ai, _ = strconv.Atoi(ap[i])
		}
		if i < len(bp) {
			bi, _ = strconv.Atoi(bp[i])
		}
		if ai != bi {
			return ai - bi
		}
	}
	return 0
}
