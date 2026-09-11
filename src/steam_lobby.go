package main

import (
	"log"
	"syscall"
	"unsafe"
)

var (
	steamLobbyID     uint64
	steamGuestJoined bool

	dllSteamHost     *syscall.DLL
	procInit         *syscall.Proc
	procRun          *syscall.Proc
	procCreate       *syscall.Proc
	procLeave        *syscall.Proc
	procGetMembers   *syscall.Proc
	procReqList      *syscall.Proc
	procGetLobbyCnt  *syscall.Proc
	procGetLobbyByIx *syscall.Proc
	procJoin         *syscall.Proc
	procGetLobbyList *syscall.Proc
	procTestInt      *syscall.Proc
)

// Carrega steam_host.dll e resolve os símbolos que vamos usar.
func loadSteamHostDLL() bool {
	if dllSteamHost != nil {
		return true
	}

	var err error
	dllSteamHost, err = syscall.LoadDLL("C:\\Users\\Admin\\Ikemen-GO-Steamworks\\IkemenTeste\\steam_host.dll")
	if err != nil {
		log.Println("loadSteamHostDLL: não conseguiu carregar steam_host.dll:", err)
		return false
	}
	log.Println("loadSteamHostDLL: steam_host.dll carregada")

	procInit, _ = dllSteamHost.FindProc("SH_Init")
	procRun, _ = dllSteamHost.FindProc("SH_RunCallbacks")
	procCreate, _ = dllSteamHost.FindProc("SH_CreateLobby")
	procLeave, _ = dllSteamHost.FindProc("SH_LeaveLobby")
	procGetMembers, _ = dllSteamHost.FindProc("SH_GetLobbyMemberCount")

	// Novas funções para JOIN
	procReqList, _ = dllSteamHost.FindProc("SH_RequestLobbyList")
	procGetLobbyCnt, _ = dllSteamHost.FindProc("SH_GetLobbyCount")
	procGetLobbyByIx, _ = dllSteamHost.FindProc("SH_GetLobbyByIndex")
	procJoin, _ = dllSteamHost.FindProc("SH_JoinLobby")
	procGetLobbyList, _ = dllSteamHost.FindProc("SH_GetLobbyList")
	procTestInt, _ = dllSteamHost.FindProc("SH_TestInt")

	if procInit == nil || procCreate == nil || procGetMembers == nil || procRun == nil {
		log.Println("loadSteamHostDLL: funções obrigatórias não encontradas")
		return false
	}
	return true
}

type lobbyInfoNative struct {
	LobbyId   uint64
	OwnerName [128]byte
}

type LobbyData struct {
	ID        uint64
	OwnerName string
}

func (li *lobbyInfoNative) ownerNameString() string {
	n := 0
	for n < len(li.OwnerName) && li.OwnerName[n] != 0 {
		n++
	}
	return string(li.OwnerName[:n])
}

// Chamado pelo Lua quando o player escolhe HOST GAME no Steam Lobby.
func SteamCreateLobbyGo(maxPlayers int) (uint64, error) {
	log.Printf("SteamCreateLobbyGo: maxPlayers=%d\n", maxPlayers)
	if !loadSteamHostDLL() {
		return 0, nil
	}

	// Inicializa a Steam via DLL
	r1, _, _ := procInit.Call()
	if r1 == 0 {
		log.Println("SteamCreateLobbyGo: SH_Init falhou (Steam não está rodando ou AppID não configurado)")
		return 0, nil
	}

	// Cria o lobby
	r2, _, _ := procCreate.Call(uintptr(maxPlayers))
	id := uint64(r2)

	// Enquanto não tivermos callback implementado na DLL,
	// SH_CreateLobby provavelmente retornará 0. Para não quebrar
	// o fluxo do Lua, podemos usar um ID fake temporário.
	if id == 0 {
		log.Println("SteamCreateLobbyGo: SH_CreateLobby retornou 0 (falha ao criar lobby)")
		return 0, nil
	}

	steamLobbyID = id
	steamGuestJoined = false
	log.Printf("SteamCreateLobbyGo: retornando ID=%d\n", steamLobbyID)

	return steamLobbyID, nil
}

// Chamado quando o host cancela o lobby (ESC) ou sai.
func SteamCloseLobbyGo() {
	if steamLobbyID == 0 {
		return
	}
	if !loadSteamHostDLL() || procLeave == nil {
		return
	}

	procLeave.Call(uintptr(steamLobbyID))
	log.Printf("SteamCloseLobbyGo: saiu do lobby %d\n", steamLobbyID)

	steamLobbyID = 0
	steamGuestJoined = false
}

// Chamado pelo loop do Lua enquanto está na tela "Aguardando oponente entrar..."
func SteamLobbyHasGuestGo() bool {
	if steamLobbyID == 0 {
		return false
	}
	if !loadSteamHostDLL() || procRun == nil || procGetMembers == nil {
		return false
	}

	// Processa callbacks da Steam (lê eventos de lobby, etc.)
	procRun.Call()

	// Pergunta para a DLL quantos membros tem no lobby
	r1, _, _ := procGetMembers.Call(uintptr(steamLobbyID))
	n := int(r1)

	if n > 1 {
		steamGuestJoined = true
		return true
	}
	return false
}

// Busca a lista de lobbies disponíveis na Steam via DLL.
// Retorna um slice com ID e nome do dono.
func SteamListLobbiesGo() ([]LobbyData, error) {
	if !loadSteamHostDLL() {
		return nil, nil
	}

	if procTestInt != nil {
		rTest, _, errT := procTestInt.Call(uintptr(123))
		log.Printf("SteamListLobbiesGo: SH_TestInt(123) returned %d (err=%v)\n", rTest, errT)
	} else {
		log.Println("SteamListLobbiesGo: procTestInt is nil")
	}

	if procReqList == nil || procGetLobbyList == nil {
		log.Println("SteamListLobbiesGo: funções de lista não disponíveis na DLL")
		return nil, nil
	}

	// garante Steam inicializada
	r0, _, _ := procInit.Call()
	if r0 == 0 {
		log.Println("SteamListLobbiesGo: SH_Init falhou (Steam não está rodando ou AppID não configurado)")
		return []LobbyData{}, nil
	}

	// pede para a DLL atualizar a lista
	r1, _, _ := procReqList.Call()
	if r1 == 0 {
		log.Println("SteamListLobbiesGo: SH_RequestLobbyList falhou")
		return []LobbyData{}, nil
	}

	const maxLobbies = 64
	var buf [maxLobbies]lobbyInfoNative

	ptr := unsafe.Pointer(&buf[0])
	r2, _, err2 := procGetLobbyList.Call(
		uintptr(ptr),
		uintptr(maxLobbies),
	)

	if err2 != nil && err2 != syscall.Errno(0) {
		log.Println("SteamListLobbiesGo: SH_GetLobbyList erro:", err2)
		return []LobbyData{}, err2
	}

	count := int(r2)
	if count <= 0 {
		log.Println("SteamListLobbiesGo: nenhum lobby retornado")
		return []LobbyData{}, nil
	}

	lobbies := make([]LobbyData, 0, count)
	for i := 0; i < count; i++ {
		li := &buf[i]
		if li.LobbyId != 0 {
			lobbies = append(lobbies, LobbyData{
				ID:        li.LobbyId,
				OwnerName: li.ownerNameString(),
			})
		}
	}

	log.Printf("SteamListLobbiesGo: %d lobbies encontrados\n", len(lobbies))
	return lobbies, nil
}

// Tenta entrar em um lobby específico.
// Retorna true em caso de sucesso.
func SteamJoinLobbyGo(id uint64) bool {
	if !loadSteamHostDLL() {
		return false
	}
	if procJoin == nil {
		log.Println("SteamJoinLobbyGo: procJoin é nil")
		return false
	}

	r1, _, _ := procJoin.Call(uintptr(id))
	ok := r1 != 0
	if ok {
		log.Printf("SteamJoinLobbyGo: entrou no lobby %d\n", id)
		steamLobbyID = id
	} else {
		log.Printf("SteamJoinLobbyGo: falha ao entrar no lobby %d\n", id)
	}
	return ok
}
