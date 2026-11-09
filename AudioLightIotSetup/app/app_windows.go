package app

import (
	"AudioLightIotSetup/utils"

	"github.com/wailsapp/wails/v3/pkg/application"
)

// -----------------------------------------------------------------------------

func addPerOsApplicationOptions(opts *application.Options) error {
	dir, err := utils.EnsureDataDir("UI")
	if err != nil {
		return err
	}

	opts.Windows = application.WindowsOptions{
		WndClass:            "AudioLightIotSetupClassWnd",
		WebviewUserDataPath: dir,
	}

	// Done
	return nil
}

func addPerOsWebviewWindowOptions(opts *application.WebviewWindowOptions) error {
	opts.Windows = application.WindowsWindow{
		NonClientRegionSupport: true,
	}

	// Done
	return nil
}
