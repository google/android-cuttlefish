package cvd

import (
	"archive/tar"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"path/filepath"
	"strings"
)

type RuntimeArtifactsManager interface {
	// Returns the file path of a tar file containing the runtime files.
	Tar() (string, error)
}

func NewRuntimeArtifactsManager(rootDir string) RuntimeArtifactsManager {
	return &runtimeArtifactsManager{RootDir: rootDir}
}

type runtimeArtifactsManager struct {
	RootDir string
}

func (m *runtimeArtifactsManager) Tar() (string, error) {
	f, err := ioutil.TempFile("", "cuttlefishRuntimeArtifacts")
	defer f.Close()
	if err != nil {
		return "", err
	}
	if err := writeTar(f, m.RootDir); err != nil {
		return "", err
	}
	if err := f.Close(); err != nil {
		return "", err
	}
	return f.Name(), nil
}

func writeTar(w io.Writer, rootDir string) error {
	tw := tar.NewWriter(w)
	err := filepath.Walk(rootDir, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return fmt.Errorf("failure accessing path %q: %w", path, err)
		}
		if info.Mode()&os.ModeType != 0 {
			return nil
		}
		if strings.HasSuffix(info.Name(), ".img") {
			return nil
		}
		if !strings.HasPrefix(path, rootDir+"/cuttlefish/instances/cvd-") {
			return nil
		}
		nameInTar := strings.TrimPrefix(path, rootDir+"/cuttlefish/instances/")
		if err := appendFile(tw, path, nameInTar, info); err != nil {
			return fmt.Errorf("failed appending file %q to tar: %w", nameInTar, err)
		}
		return nil
	})
	if err != nil {
		return err
	}
	return tw.Close()
}

func appendFile(tw *tar.Writer, path string, name string, info os.FileInfo) error {
	f, err := os.Open(path)
	if err != nil {
		return err
	}
	defer f.Close()
	hdr := &tar.Header{
		Name: name,
		Mode: int64(info.Mode()),
		Size: info.Size(),
	}
	if err := tw.WriteHeader(hdr); err != nil {
		return err
	}
	if _, err := io.Copy(tw, f); err != nil {
		return err
	}
	return nil
}
