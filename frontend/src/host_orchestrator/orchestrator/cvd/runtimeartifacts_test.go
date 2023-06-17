package cvd

import (
	"archive/tar"
	"bufio"
	"bytes"
	"io"
	"os"
	"path/filepath"
	"runtime"
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestTar(t *testing.T) {
	expected := []string{
		"cvd-1/NVChip",
		"cvd-1/access-kregistry",
		"cvd-1/ap_composite_disk_config.txt",
		"cvd-1/cuttlefish_config.json",
		"cvd-1/iccprofile_for_sim0.xml",
		"cvd-1/internal/bootconfig",
		"cvd-1/logs/kernel.log",
		"cvd-1/logs/logcat",
		"cvd-2/NVChip",
	}
	m := NewRuntimeArtifactsManager(fakeRuntimesDir())

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
		var b bytes.Buffer
		if _, err := io.Copy(bufio.NewWriter(&b), tr); err != nil {
			t.Error(err)
		}
		if diff := cmp.Diff("[redacted]\n", b.String()); diff != "" {
			t.Errorf("file content mismatch (-want +got):\n%s", diff)
		}
	}
	if diff := cmp.Diff(expected, names); diff != "" {
		t.Errorf("files in tar mismatch (-want +got):\n%s", diff)
	}
}

func fakeRuntimesDir() string {
	_, filename, _, _ := runtime.Caller(0)
	dir := filepath.Dir(filename)
	return dir + "/testdata/runtimes"
}
