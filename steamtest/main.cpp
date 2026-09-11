#include "steam/steam_api.h"
#include <iostream>

int main() {
    std::cout << "Steam test starting...\n";

    if (!SteamAPI_IsSteamRunning()) {
        std::cout << "Steam is not running.\n";
        return 1;
    }

    if (!SteamAPI_Init()) {
        std::cout << "SteamAPI_Init failed.\n";
        return 1;
    }

    std::cout << "SteamAPI_Init OK!\n";

    // Apenas roda callbacks um pouquinho e sai.
    for (int i = 0; i < 10; i++) {
        SteamAPI_RunCallbacks();
    }

    SteamAPI_Shutdown();
    std::cout << "SteamAPI_Shutdown done.\n";
    return 0;
}