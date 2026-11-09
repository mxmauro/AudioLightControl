package app

import (
	"sync/atomic"
)

// -----------------------------------------------------------------------------

// CloseControlService grants the frontend permission for exactly one native window close.
type CloseControlService struct {
	allowClose   atomic.Bool
	runtimeReady atomic.Bool
}

// -----------------------------------------------------------------------------

// AllowClose permits the frontend's immediately following Window.Close call.
func (s *CloseControlService) AllowClose() {
	s.allowClose.Store(true)
}

func (s *CloseControlService) consumeAllowClose() bool {
	return s.allowClose.CompareAndSwap(true, false)
}

func (s *CloseControlService) markRuntimeReady() {
	s.runtimeReady.Store(true)
}

func (s *CloseControlService) isRuntimeReady() bool {
	return s.runtimeReady.Load()
}
