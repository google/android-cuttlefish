package cvd

import (
	"archive/tar"
	"io"
	"os"
	"path/filepath"
	"testing"

	orchtesting "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/orchestrator/testing"

	"github.com/google/go-cmp/cmp"
)

func TestTar(t *testing.T) {
	dir := orchtesting.TempDir(t)
	defer orchtesting.RemoveDir(t, dir)
	createFakeRuntimeDir(t, dir)
	expected := []string{
		"cvd-1/assemble_cvd.log",
		"cvd-1/disk_config.txt",
		"cvd-1/logs/kernel.log",
		"cvd-128/assemble_cvd.log",
	}
	m := NewRuntimeArtifactsManager(dir)

	tarFile, err := m.Tar()

	if err != nil {
		t.Fatal(err)
	}
	defer os.Remove(tarFile)

	f, err := os.Open(tarFile)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	names := []string{}
	tr := tar.NewReader(f)
	for {
		hdr, err := tr.Next()
		if err == io.EOF {
			break // End of archive
		}
		if err != nil {
			t.Fatal(err)
		}
		names = append(names, hdr.Name)
	}
	if diff := cmp.Diff(expected, names); diff != "" {
		t.Errorf("files in tar mismatch (-want +got):\n%s", diff)
	}
}

type runtimeItemType int

const (
	dir     runtimeItemType = 0
	file    runtimeItemType = 1
	symlink runtimeItemType = 2
)

type runtimeItem struct {
	Name          string
	Type          runtimeItemType
	SymlinkTarget string
}

func createFakeRuntimeDir(t *testing.T, rootDir string) {
	items := []runtimeItem{
		{Name: "cuttlefish", Type: dir},
		{Name: "cuttlefish/assembly", Type: dir},
		{Name: "cuttlefish/assembly/assemble_cvd.log", Type: file},
		{Name: "cuttlefish/instances", Type: dir},
		{Name: "cuttlefish/instances/config.json", Type: file},
		{Name: "cuttlefish/instances/cvd-1", Type: dir},
		{Name: "cuttlefish/instances/cvd-1/assemble_cvd.log", Type: file},
		{Name: "cuttlefish/instances/cvd-1/disk_config.txt", Type: file},
		{Name: "cuttlefish/instances/cvd-1/ap_composite_disk_config.txt", Type: file},
		{Name: "cuttlefish/instances/cvd-1/misc.img", Type: file},
		{Name: "cuttlefish/instances/cvd-1/logs", Type: dir},
		{Name: "cuttlefish/instances/cvd-1/logs/kernel.log", Type: file},
		{
			Name:          "cuttlefish/instances/cvd-1/kernel.log",
			Type:          symlink,
			SymlinkTarget: "cuttlefish/instances/cvd-1/logs/kernel.log",
		},
		{Name: "cuttlefish/instances/cvd-128", Type: dir},
		{Name: "cuttlefish/instances/cvd-128/assemble_cvd.log", Type: file},
	}
	for _, it := range items {
		name := filepath.Join(rootDir, it.Name)
		switch it.Type {
		case dir:
			if err := os.Mkdir(name, 0755); err != nil {
				t.Fatal(err)
			}
		case file:
			if err := orchtesting.WriteFile(name, []byte("foo\n"), 0644); err != nil {
				t.Fatal(err)
			}
		case symlink:
			target := filepath.Join(rootDir, it.SymlinkTarget)
			if err := os.Symlink(target, name); err != nil {
				t.Fatal(err)
			}
		}
	}
}
