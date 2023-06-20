package testing

import (
	"io/ioutil"
	"os"
	"testing"
)

// Creates a temporary directory for the test to use returning its path.
// Each subsequent call creates a unique directory; if the directory creation
// fails, `tempDir` terminates the test by calling Fatal.
//
// Similar to https://pkg.go.dev/testing#T.TempDir without the cleanup part. testing#T.TempDir cannot be used
// as it was introduced in go 1.15.
func TempDir(t *testing.T) string {
	name, err := ioutil.TempDir("", "cuttlefishTestDir")
	if err != nil {
		t.Fatal(err)
	}
	return name
}

// Removes the directory at the passed path.
// If deletion fails, `removeDir` terminates the test by calling Fatal.
func RemoveDir(t *testing.T, name string) {
	if err := os.RemoveAll(name); err != nil {
		t.Fatal(err)
	}
}
