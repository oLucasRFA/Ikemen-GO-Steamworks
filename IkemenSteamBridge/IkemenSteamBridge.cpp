#include "C:/Users/Admin/Ikemen-GO-Steamworks/IkemenTeste/steamworks/public/steam/steam_api.h"

extern "C" __declspec(dllexport) int SteamInit() {
    if (SteamAPI_Init()) {
        return 1;
    }
    return 0;
}

extern "C" __declspec(dllexport) void SteamShutdown() {
    SteamAPI_Shutdown();
}

extern "C" __declspec(dllexport) int SteamCreateLobby() {
    // Por enquanto, só uma função de teste.
    // Depois colocamos a lógica real de SteamMatchmaking aqui.
    return 1;
}