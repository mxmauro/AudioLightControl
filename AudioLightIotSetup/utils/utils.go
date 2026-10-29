package utils

import (
	"os"
	"path/filepath"
	"runtime"
	"strings"
)

// -----------------------------------------------------------------------------

func EnsureDir(dir string) error {
	_, err := os.Stat(dir)
	if err != nil {
		if os.IsNotExist(err) {
			// path/to/whatever does not exist
			if err = os.MkdirAll(dir, 0755); err != nil {
				return err
			}
		} else {
			return err
		}
	}
	return nil
}

func GetDataDir(subdir string) (string, error) {
	basedir, err := os.UserCacheDir()
	if err != nil {
		return "", err
	}

	// Done
	if runtime.GOOS == "windows" {
		return filepath.Join(basedir, "AudioLight-Iot-Setup", subdir), nil
	}
	return filepath.Join(basedir, "audiolight-iot-setup", strings.ToLower(subdir)), nil
}

func EnsureDataDir(subdir string) (string, error) {
	dir, err := GetDataDir(subdir)
	if err != nil {
		return "", err
	}

	err = EnsureDir(dir)
	if err != nil {
		return "", err
	}

	// Done
	return dir, nil
}
