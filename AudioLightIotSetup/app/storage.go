package app

import (
	"context"
	"errors"
	"os"
	"path/filepath"
	"regexp"

	"AudioLightIotSetup/utils"

	"github.com/wailsapp/wails/v3/pkg/application"
)

// -----------------------------------------------------------------------------

type StorageService struct {
	baseDir string
}

// -----------------------------------------------------------------------------

var reFilename = regexp.MustCompile("^[a-zA-Z0-9_]+\\.[a-zA-Z0-9_]+$")

// -----------------------------------------------------------------------------

func (s *StorageService) ServiceStartup(_ context.Context, _ application.ServiceOptions) error {
	var err error

	s.baseDir, err = utils.GetDataDir("Data")
	if err != nil {
		return err
	}

	// Done
	return nil
}

func (s *StorageService) ServiceShutdown() error {
	s.baseDir = ""

	// Done
	return nil
}

func (s *StorageService) Load(filename string) (string, error) {
	if !reFilename.MatchString(filename) {
		return "", errors.New("invalid filename")
	}
	filename = filepath.Join(s.baseDir, filename)

	// Load data
	data, err := os.ReadFile(filename)
	if err != nil {
		if os.IsNotExist(err) {
			return "", errors.New("file not found")
		}
		return "", err
	}

	// Done
	return string(data), nil
}

func (s *StorageService) Save(filename string, content string) error {
	if !reFilename.MatchString(filename) {
		return errors.New("invalid filename")
	}
	filename = filepath.Join(s.baseDir, filename)

	// Ensure the directory exists
	err := utils.EnsureDir(s.baseDir)
	if err != nil {
		return err
	}

	// Write file
	err = os.WriteFile(filename, []byte(content), 0644)
	if err != nil {
		return err
	}

	// Done
	return nil
}
