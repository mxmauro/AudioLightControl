package app

import (
	"context"
	"embed"
	"strconv"
	"sync"

	"AudioLightIotSetup/utils"
	"AudioLightIotSetup/utils/storage"

	"github.com/wailsapp/wails/v2/pkg/application"
	wailslogger "github.com/wailsapp/wails/v2/pkg/logger"
	"github.com/wailsapp/wails/v2/pkg/options"
	"github.com/wailsapp/wails/v2/pkg/options/assetserver"
	wailsruntime "github.com/wailsapp/wails/v2/pkg/runtime"
)

// -----------------------------------------------------------------------------

// App struct
type App struct {
	ctx context.Context

	app *application.Application
	stg *storage.Storage

	closeMu       sync.Mutex
	closeApproved bool
}

// -----------------------------------------------------------------------------

func Run(assets embed.FS) error {
	sessionID, err := utils.GetSessionID()
	if err != nil {
		return err
	}

	a := App{}

	a.stg, err = storage.New()
	if err != nil {
		return err
	}

	opts := options.App{
		Title:            "AudioLight IoT Setup",
		Width:            1024,
		Height:           800,
		MinWidth:         1024,
		MinHeight:        800,
		WindowStartState: options.Normal,
		Frameless:        true,
		AssetServer: &assetserver.Options{
			Assets: assets,
		},
		BackgroundColour: &options.RGBA{
			R: 0x09, G: 0x12, B: 0x24, A: 255,
		},
		OnStartup:     a.onAppStartup,
		OnShutdown:    a.onAppShutdown,
		OnBeforeClose: a.onBeforeClose,
		Bind: []any{
			&a,
			a.stg,
		},
		// Logger:             logger.WrapWailsLogger(lg),
		LogLevel:           wailslogger.TRACE,
		LogLevelProduction: wailslogger.ERROR,
		SingleInstanceLock: &options.SingleInstanceLock{
			UniqueId:               "AudioLightIotSetup-" + strconv.Itoa(sessionID),
			OnSecondInstanceLaunch: a.onSecondInstanceLaunch,
		},
	}
	err = addPerOsConfig(&opts)
	if err != nil {
		return err
	}

	// Create application and run it
	a.app = application.NewWithOptions(&opts)
	err = a.app.Run()
	a.app = nil

	// Done
	return err
}

/*
func (a *App) GetDmxFixtures() []dmx.Fixture {
	return a.pdf.Fixtures
}

func (a *App) AddDmxFixture(_type string, channels []string, replace bool) string {
	err := a.pdf.Add(_type, channels, replace)
	if err != nil {
		return err.Error()
	}
	return ""
}

func (a *App) RemoveDmxFixture(_type string) string {
	err := a.pdf.Remove(_type)
	if err != nil {
		return err.Error()
	}
	return ""
}
*/

func (a *App) onAppShutdown(_ context.Context) {
}

func (a *App) ConfirmClose() {
	// Allow the next native close after the frontend's leave guard has approved it.
	a.closeMu.Lock()
	a.closeApproved = true
	a.closeMu.Unlock()

	wailsruntime.Quit(a.ctx)
}

func (a *App) onBeforeClose(ctx context.Context) bool {
	// Keep the window open until the frontend resolves any unsaved changes.
	a.closeMu.Lock()
	approved := a.closeApproved
	a.closeMu.Unlock()
	if approved {
		return false
	}

	wailsruntime.EventsEmit(ctx, "app:close-requested")
	return true
}

func (a *App) onSecondInstanceLaunch(_ options.SecondInstanceData) {
	wailsruntime.WindowShow(a.ctx)
}

// startup is called when the app starts. The context is saved
// so we can call the runtime methods
func (a *App) onAppStartup(ctx context.Context) {
	a.ctx = ctx
}
