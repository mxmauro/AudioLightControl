package storage

import (
	"os"
	"path/filepath"
	"regexp"

	"AudioLightIotSetup/utils"
)

// -----------------------------------------------------------------------------

type Storage struct {
	baseDir string
}

type LoadResult struct {
	Content string
	Error   string
}

// -----------------------------------------------------------------------------

var reFilename = regexp.MustCompile("^[a-zA-Z0-9_]+\\.[a-zA-Z0-9_]+$")

// -----------------------------------------------------------------------------

func New() (*Storage, error) {
	dir, err := utils.GetDataDir("Data")
	if err != nil {
		return nil, err
	}

	stg := &Storage{
		baseDir: dir,
	}

	// Done
	return stg, nil
}

func (stg *Storage) Load(filename string) LoadResult {
	if !reFilename.MatchString(filename) {
		return LoadResult{
			Error: "invalid filename",
		}
	}
	filename = filepath.Join(stg.baseDir, filename)

	// Load data
	data, err := os.ReadFile(filename)
	if err != nil {
		if os.IsNotExist(err) {
			return LoadResult{
				Error: "not found",
			}
		}
		return LoadResult{
			Error: err.Error(),
		}
	}

	// Done
	return LoadResult{
		Content: string(data),
	}
}

func (stg *Storage) Save(filename string, content string) string {
	if !reFilename.MatchString(filename) {
		return "invalid filename"
	}
	filename = filepath.Join(stg.baseDir, filename)

	// Ensure the directory exists
	err := utils.EnsureDir(stg.baseDir)
	if err != nil {
		return err.Error()
	}

	// Write file
	err = os.WriteFile(filename, []byte(content), 0644)
	if err != nil {
		return err.Error()
	}

	// Done
	return ""
}
