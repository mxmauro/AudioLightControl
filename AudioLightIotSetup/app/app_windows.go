package app

import (
	"AudioLightIotSetup/utils"

	"github.com/wailsapp/wails/v2/pkg/options"
	wailswindows "github.com/wailsapp/wails/v2/pkg/options/windows"
)

// -----------------------------------------------------------------------------

func addPerOsConfig(opts *options.App) error {
	dir, err := utils.EnsureDataDir("UI")
	if err != nil {
		return err
	}

	opts.Windows = &wailswindows.Options{
		WebviewUserDataPath: dir,
		WindowClassName:     "AudioLightIotSetupClassWnd",
	}

	// Done
	return nil
}
