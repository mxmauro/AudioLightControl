package main

import (
	"context"
	"embed"
	"errors"
	"os"

	"AudioLightIotSetup/app"
)

// -----------------------------------------------------------------------------

//go:embed all:frontend/dist
var assets embed.FS

// -----------------------------------------------------------------------------

func main() {
	if !_main() {
		os.Exit(1)
	}
}

func _main() bool {
	// Run application
	err := app.Run(assets)
	if err != nil && !errors.Is(err, context.Canceled) {
		return false
	}

	// Exit without error
	return true
}
