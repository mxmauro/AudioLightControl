package app

import (
	"embed"
	"log/slog"
	"strconv"

	"AudioLightIotSetup/utils"

	"github.com/wailsapp/wails/v3/pkg/application"
	"github.com/wailsapp/wails/v3/pkg/events"
)

// -----------------------------------------------------------------------------

func Run(assets embed.FS) error {
	var mainWnd *application.WebviewWindow

	sessionID, err := utils.GetSessionID()
	if err != nil {
		return err
	}

	closeControl := &CloseControlService{}

	// Create application
	appOpts := application.Options{
		Name: "AudioLight IoT Setup",
		Services: []application.Service{
			application.NewService(&StorageService{}),
			application.NewService(closeControl),
		},
		Assets: application.AssetOptions{
			Handler: application.AssetFileServerFS(assets),
		},

		LogLevel: slog.LevelWarn,
		SingleInstance: &application.SingleInstanceOptions{
			UniqueID: "AudioLightIotSetup-" + strconv.Itoa(sessionID),
			OnSecondInstanceLaunch: func(data application.SecondInstanceData) {
				// Focus the window if needed
				if mainWnd != nil {
					mainWnd.Restore()
					mainWnd.Focus()
				}
			},
		},
	}
	err = addPerOsApplicationOptions(&appOpts)
	if err != nil {
		return err
	}

	app := application.New(appOpts)

	// Create main window
	wndOpts := application.WebviewWindowOptions{
		Name:       "main-window",
		Title:      "AudioLight IoT Setup",
		Width:      1024,
		Height:     800,
		MinWidth:   1024,
		MinHeight:  800,
		StartState: application.WindowStateNormal,
		Frameless:  true,
		Permissions: map[application.PermissionType]application.Permission{
			application.PermissionNotifications: application.PermissionAllow,
		},
		BackgroundColour: application.RGBA{
			Red: 0x09, Green: 0x12, Blue: 0x24, Alpha: 255,
		},
	}
	err = addPerOsWebviewWindowOptions(&wndOpts)
	if err != nil {
		return err
	}

	mainWnd = app.Window.NewWithOptions(wndOpts)
	mainWnd.OnWindowEvent(events.Common.WindowRuntimeReady, func(_ *application.WindowEvent) {
		closeControl.markRuntimeReady()
	})
	mainWnd.RegisterHook(events.Common.WindowClosing, func(event *application.WindowEvent) {
		if !closeControl.isRuntimeReady() {
			return
		}
		if closeControl.consumeAllowClose() {
			return
		}
		event.Cancel()
		mainWnd.EmitEvent("app:close-requested")
	})

	// Run application
	err = app.Run()

	// Done
	return err
}
