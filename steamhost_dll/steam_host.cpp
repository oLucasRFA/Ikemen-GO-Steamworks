#include "steam/steam_api.h"
#include <thread>
#include <chrono>
#include <vector>
#include <iostream>
#include <fstream>
#include <mutex>

static std::mutex g_logMutex;

static void LogToFile(const char* msg) {
    std::lock_guard<std::mutex> lock(g_logMutex);

    // grava no arquivo "steam_host.log" na pasta atual do processo (a do jogo)
    std::ofstream f("steam_host.log", std::ios::app);
    if (!f.is_open()) return;

    f << msg << std::endl;
}

#define DLL_EXPORT extern "C" __declspec(dllexport)

static bool      g_steamInitialized   = false;
static CSteamID  g_currentLobbyID;
static bool      g_lobbyCreated       = false;
static EResult   g_lobbyCreateResult  = k_EResultFail;
static bool             g_lobbyListReceived   = false;
static std::vector<CSteamID> g_lobbyList;
static EResult          g_lobbyListResult     = k_EResultFail;
static bool                    g_lobbyEntered      = false;
static EChatRoomEnterResponse  g_lobbyEnterResult  = k_EChatRoomEnterResponseError;

static const int MAX_STEAM_NAME = 128;

struct LobbyInfo {
    uint64_t lobbyId;
    char ownerName[MAX_STEAM_NAME];
};

static LobbyInfo g_lobbies[64];
static int g_lobbyCount = 0;

class LobbyCreatedCallbackHandler {
public:
    CCallback<LobbyCreatedCallbackHandler, LobbyCreated_t> m_CallbackLobbyCreated;

    LobbyCreatedCallbackHandler()
        : m_CallbackLobbyCreated( this, &LobbyCreatedCallbackHandler::OnLobbyCreated ) {}

    void OnLobbyCreated( LobbyCreated_t *pCallback );
};

void LobbyCreatedCallbackHandler::OnLobbyCreated( LobbyCreated_t *pCallback ) {
    g_lobbyCreateResult = pCallback->m_eResult;

    if ( pCallback->m_eResult == k_EResultOK ) {
        g_currentLobbyID = pCallback->m_ulSteamIDLobby;
        g_lobbyCreated   = true;
    } else {
        g_currentLobbyID = CSteamID();
        g_lobbyCreated   = false;
    }
}

static LobbyCreatedCallbackHandler g_lobbyCreatedHandler;

class LobbyMatchListCallbackHandler {
public:
    CCallback<LobbyMatchListCallbackHandler, LobbyMatchList_t> m_CallbackLobbyMatchList;

    LobbyMatchListCallbackHandler()
        : m_CallbackLobbyMatchList( this, &LobbyMatchListCallbackHandler::OnLobbyMatchList ) {}

    void OnLobbyMatchList( LobbyMatchList_t *pCallback );
};

void LobbyMatchListCallbackHandler::OnLobbyMatchList( LobbyMatchList_t *pCallback ) {
    g_lobbyCount = 0;
    g_lobbyList.clear();
    g_lobbyListResult = k_EResultOK;

    int numLobbies = pCallback->m_nLobbiesMatching;
    if (numLobbies > 64) numLobbies = 64;

    {
        char buf[128];
        sprintf_s(buf, "[steam_host] MatchList callback, m_nLobbiesMatching=%d", numLobbies);
        LogToFile(buf);
    }

    for (int i = 0; i < numLobbies; ++i) {
        CSteamID lobbyId = SteamMatchmaking()->GetLobbyByIndex(i);
        CSteamID owner   = SteamMatchmaking()->GetLobbyOwner(lobbyId);

        g_lobbyList.push_back(lobbyId);
        g_lobbies[i].lobbyId = lobbyId.ConvertToUint64();

        const char* name = SteamFriends()->GetFriendPersonaName(owner);
        if (!name) name = "Desconhecido";

        strncpy_s(g_lobbies[i].ownerName, MAX_STEAM_NAME, name, _TRUNCATE);

        char buf[256];
        sprintf_s(buf, "[steam_host] lobby %d id=%llu owner=%s",
                  i, (unsigned long long)g_lobbies[i].lobbyId, g_lobbies[i].ownerName);
        LogToFile(buf);
    }

    g_lobbyCount = numLobbies;
    g_lobbyListReceived = true;
}

static LobbyMatchListCallbackHandler g_lobbyMatchListHandler;

class LobbyEnterCallbackHandler {
public:
    CCallback<LobbyEnterCallbackHandler, LobbyEnter_t> m_CallbackLobbyEnter;

    LobbyEnterCallbackHandler()
        : m_CallbackLobbyEnter( this, &LobbyEnterCallbackHandler::OnLobbyEnter ) {}

    void OnLobbyEnter( LobbyEnter_t *pCallback );
};

void LobbyEnterCallbackHandler::OnLobbyEnter( LobbyEnter_t *pCallback ) {
    g_lobbyEnterResult = static_cast<EChatRoomEnterResponse>( pCallback->m_EChatRoomEnterResponse );

    if ( g_lobbyEnterResult == k_EChatRoomEnterResponseSuccess ) {
        g_lobbyEntered   = true;
        g_currentLobbyID = pCallback->m_ulSteamIDLobby;
    } else {
        g_lobbyEntered = false;
    }
}

static LobbyEnterCallbackHandler g_lobbyEnterHandler;

DLL_EXPORT bool SH_RequestLobbyList() {
    if (!g_steamInitialized || !SteamMatchmaking()) {
        LogToFile("[steam_host] SH_RequestLobbyList: Steam não inicializado ou matchmaking nulo");
        return false;
    }

    g_lobbyList.clear();
    g_lobbyListReceived = false;
    g_lobbyListResult = k_EResultFail;
    g_lobbyCount = 0;

    SteamAPICall_t call = SteamMatchmaking()->RequestLobbyList();
    (void)call;

    const int maxTries = 300;
    const int sleepMs  = 10;

    for (int i = 0; i < maxTries; ++i) {
        SteamAPI_RunCallbacks();

        if (g_lobbyListReceived && g_lobbyListResult == k_EResultOK) {
            LogToFile("[steam_host] SH_RequestLobbyList: recebeu lista de lobbies");
            return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    }

    LogToFile("[steam_host] SH_RequestLobbyList: timeout sem receber lista");
    return false;
}

DLL_EXPORT int SH_GetLobbyCount() {
    return static_cast<int>( g_lobbyList.size() );
}

DLL_EXPORT unsigned long long SH_GetLobbyByIndex( int index ) {
    if ( index < 0 || index >= static_cast<int>( g_lobbyList.size() ) )
        return 0ULL;
    return g_lobbyList[index].ConvertToUint64();
}

DLL_EXPORT bool SH_JoinLobby( unsigned long long lobbyId ) {
    if ( !g_steamInitialized || !SteamMatchmaking() )
        return false;

    g_lobbyEntered     = false;
    g_lobbyEnterResult = k_EChatRoomEnterResponseError;

    CSteamID lobby( lobbyId );
    SteamAPICall_t call = SteamMatchmaking()->JoinLobby( lobby );
    (void)call;

    const int maxTries = 300;
    const int sleepMs  = 10;

    for ( int i = 0; i < maxTries; ++i ) {
        SteamAPI_RunCallbacks();

        if ( g_lobbyEntered && g_lobbyEnterResult == k_EChatRoomEnterResponseSuccess ) {
            return true;
        }

        std::this_thread::sleep_for( std::chrono::milliseconds( sleepMs ) );
    }

    return false;
}

// Inicializar Steam (chamar uma vez)
DLL_EXPORT bool SH_Init() {
    if (g_steamInitialized)
        return true;

    if (!SteamAPI_IsSteamRunning()) {
        return false;
    }
    if (!SteamAPI_Init()) {
        return false;
    }

    g_steamInitialized = true;
    return true;
}

// Encerrar Steam (se quiser)
DLL_EXPORT void SH_Shutdown() {
    if (!g_steamInitialized)
        return;
    SteamAPI_Shutdown();
    g_steamInitialized = false;
}

// Processar callbacks
DLL_EXPORT void SH_RunCallbacks() {
    if (!g_steamInitialized)
        return;
    SteamAPI_RunCallbacks();
}

// Cria um lobby - por enquanto só dispara CreateLobby e retorna 0
DLL_EXPORT unsigned long long SH_CreateLobby( int maxPlayers ) {
    if ( !g_steamInitialized )
        return 0ULL;

    if ( !SteamMatchmaking() )
        return 0ULL;

    // Reset do estado
    g_lobbyCreated      = false;
    g_lobbyCreateResult = k_EResultFail;
    g_currentLobbyID    = CSteamID();

    // Chama CreateLobby (assíncrono)
    SteamAPICall_t call = SteamMatchmaking()->CreateLobby( k_ELobbyTypePublic, maxPlayers );
    (void)call; // não usamos diretamente, callback cuida

    // Espera o callback chegar por um tempo limitado
    const int maxTries = 300;       // por exemplo, 300 iterações
    const int sleepMs  = 10;        // 10 ms cada → ~3s Máx

    for ( int i = 0; i < maxTries; i++ ) {
        SteamAPI_RunCallbacks();

        if ( g_lobbyCreated && g_lobbyCreateResult == k_EResultOK ) {
            return g_currentLobbyID.ConvertToUint64();
        }

        // Se houve erro
        if ( g_lobbyCreateResult != k_EResultOK && g_lobbyCreateResult != k_EResultFail ) {
            break;
        }

        // Pequeno sleep pra não travar 100% a CPU
        std::this_thread::sleep_for( std::chrono::milliseconds( sleepMs ) );
    }

    // Se chegou aqui, não conseguiu criar
    return 0ULL;
}
// Sair do lobby
DLL_EXPORT void SH_LeaveLobby(unsigned long long lobbyId) {
    if (!g_steamInitialized || !SteamMatchmaking())
        return;
    CSteamID lobby(lobbyId);
    SteamMatchmaking()->LeaveLobby(lobby);
}

// Número de membros no lobby
DLL_EXPORT int SH_GetLobbyMemberCount(unsigned long long lobbyId) {
    if (!g_steamInitialized || !SteamMatchmaking())
        return 0;
    CSteamID lobby(lobbyId);
    return SteamMatchmaking()->GetNumLobbyMembers(lobby);
}

DLL_EXPORT int SH_TestInt(int x) {
    LogToFile("[steam_host] SH_TestInt called");
    
    char buf[128];
    sprintf_s(buf, "[steam_host] SH_TestInt arg=%d", x);
    LogToFile(buf);

    return x + 1;
}

extern "C" __declspec(dllexport)
int SH_GetLobbyList(LobbyInfo* out, int max) {
    char buf[128];
    sprintf_s(buf, "[steam_host] SH_GetLobbyList: begin, out=%p max=%d", (void*)out, max);
    LogToFile(buf);

    if (!out || max <= 0) {
        LogToFile("[steam_host] SH_GetLobbyList: out nulo ou max <= 0");
        return 0;
    }

    if (max > 64) max = 64;

    int count = g_lobbyCount;
    if (count > 64) count = 64;
    if (count > max) count = max;

    sprintf_s(buf, "[steam_host] SH_GetLobbyList: copiando %d entries", count);
    LogToFile(buf);

    for (int i = 0; i < count; ++i) {
        out[i] = g_lobbies[i];
    }

    LogToFile("[steam_host] SH_GetLobbyList: end");
    return count;
}