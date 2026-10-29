package utils

import (
	"golang.org/x/sys/windows"
)

// -----------------------------------------------------------------------------

func GetSessionID() (int, error) {
	var sessionID uint32

	pid := windows.GetCurrentProcessId()
	err := windows.ProcessIdToSessionId(pid, &sessionID)
	if err != nil {
		return 0, err
	}
	return int(uint(sessionID)), nil
}
