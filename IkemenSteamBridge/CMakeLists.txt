cmake_minimum_required(VERSION 3.15)

project(IkemenSteamBridge LANGUAGES CXX)

add_library(IkemenSteamBridge SHARED IkemenSteamBridge.cpp)

# Headers do Steamworks
include_directories("C:/Users/Admin/Ikemen-GO-Steamworks/IkemenTeste/steamworks/public")

# Linkar a lib da Steam (ajuste o caminho se o seu for diferente)
target_link_libraries(IkemenSteamBridge
    "C:/Users/Admin/Ikemen-GO-Steamworks/IkemenTeste/steamworks/redistributable_bin/win64/steam_api64.lib"
)