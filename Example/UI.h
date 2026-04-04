#pragma once
#define NOMINMAX
#include <Windows.h>
#include <d3d11.h>
#include <string>
#include "imgui.h"

namespace UI {
    // Premium Menu & Styles
    void SetupStyles();
    void RenderMenu();
    void RenderPrimeHUD();
    void RenderESP();
    
    // External Globals (State and Config)
    extern bool bShowMenu;
    extern bool bShowPlayers;
    extern bool bShowAnimals;
    extern bool bShowFish;
    extern bool bShowCarcass;
    extern bool bShowNests;
    extern bool bShowBars;
    extern bool bShowBones;
    extern bool bShowPrime;
    extern int iMaxDist;
}
