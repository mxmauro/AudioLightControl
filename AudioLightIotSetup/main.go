package main

import (
	"embed"
	"os"

	"AudioLightIotSetup/app"
)

// -----------------------------------------------------------------------------

//go:embed all:frontend/dist
var assets embed.FS

// //go:embed build/icon.png
// var appIcon []byte

// //go:embed wails.json
// var wailsConfigJSON []byte

// -----------------------------------------------------------------------------

func main() {
	if !_main() {
		os.Exit(1)
	}
}

func _main() bool {
	// Run application
	err := app.Run(assets)
	if err != nil {
		return false
	}

	// Exit without error
	return true
}
