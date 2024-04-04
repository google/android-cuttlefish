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

// Same as https://pkg.go.dev/os#WriteFile, os#WriteFile cannot be used as it was introduced in go 1.16.
func WriteFile(name string, data []byte, perm os.FileMode) error {
	f, err := os.OpenFile(name, os.O_WRONLY|os.O_CREATE|os.O_TRUNC, perm)
	if err != nil {
		return err
	}
	_, err = f.Write(data)
	if err1 := f.Close(); err1 != nil && err == nil {
		err = err1
	}
	return err
}

// Helper to use 1 line rather than 3-lines block:
// ```
//
//	if err != nil {
//	    t.Fatal(err)
//	}
//
// ```
// in unit test functions.
func FatalIfNil(t *testing.T, err error) {
	if err != nil {
		t.Fatal(err)
	}
}
