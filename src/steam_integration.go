//go:build windows
// +build windows

package main

import "syscall"

var (
	steamBridge          = syscall.NewLazyDLL("IkemenSteamBridge.dll")
	steamInitProc        = steamBridge.NewProc("SteamInit")
	steamShutdownProc    = steamBridge.NewProc("SteamShutdown")
	steamCreateLobbyProc = steamBridge.NewProc("SteamCreateLobby") // precisa desta linha
)

func InitSteam() bool {
	r1, _, _ := steamInitProc.Call()
	return r1 != 0
}

func ShutdownSteam() {
	steamShutdownProc.Call()
}

// ESTA função precisa existir:
func SteamCreateLobby() bool {
	r1, _, _ := steamCreateLobbyProc.Call()
	return r1 != 0
}
