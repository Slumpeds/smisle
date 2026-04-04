#include "UI.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include <cstdint>
#include <vector>

extern uint8_t g_primeData[11]; // Defined in Example.cpp

namespace UI {
// Globals
bool bShowMenu = true;
bool bShowPlayers = true;
bool bShowAnimals = true;
bool bShowFish = false;
bool bShowCarcass = true;
bool bShowNests = false;
bool bShowBars = true;
bool bShowBones = false;
bool bShowPrime = true;
int iMaxDist = 1000;

void SetupStyles() {
  auto &style = ImGui::GetStyle();
  style.WindowRounding = 8.0f;
  style.ChildRounding = 6.0f;
  style.FrameRounding = 4.0f;
  style.GrabRounding = 4.0f;
  style.PopupRounding = 6.0f;
  style.ScrollbarRounding = 12.0f;

  ImVec4 *colors = style.Colors;
  colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.08f, 0.94f);
  colors[ImGuiCol_Header] = ImVec4(0.20f, 0.22f, 0.27f, 1.00f);
  colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
  colors[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
  colors[ImGuiCol_Button] = ImVec4(0.20f, 0.22f, 0.27f, 1.00f);
  colors[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
  colors[ImGuiCol_ButtonActive] = ImVec4(0.06f, 0.53f, 0.98f, 1.00f);
  colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.12f, 0.15f, 1.00f);
  colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.18f, 0.22f, 1.00f);
  colors[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.24f, 0.28f, 1.00f);
  colors[ImGuiCol_CheckMark] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
  colors[ImGuiCol_SliderGrab] = ImVec4(0.24f, 0.52f, 0.88f, 1.00f);
  colors[ImGuiCol_SliderGrabActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
  colors[ImGuiCol_TitleBg] = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);
  colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.12f, 0.15f, 1.00f);
}

void RenderMenu() {
  if (!bShowMenu)
    return;

  ImGui::Begin("The Isle DMA", &bShowMenu,
               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);

  if (ImGui::BeginTabBar("Tabs")) {
    if (ImGui::BeginTabItem("Visuals")) {
      ImGui::SeparatorText("Entity Filters");
      ImGui::Checkbox("Players", &bShowPlayers);
      ImGui::Checkbox("Animals (AI)", &bShowAnimals);
      ImGui::Checkbox("Fish", &bShowFish);
      ImGui::Checkbox("Carcasses & Dead", &bShowCarcass);
      ImGui::Checkbox("Nests", &bShowNests);

      ImGui::SeparatorText("Aesthetics");
      ImGui::Checkbox("Health/Stam Bars", &bShowBars);
      ImGui::Checkbox("Skeleton Bones", &bShowBones);
      ImGui::SliderInt("Max Distance", &iMaxDist, 100, 5000, "%d m");

      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Misc")) {
      ImGui::Checkbox("Prime Elder Checklist", &bShowPrime);
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::End();
}

void RenderPrimeHUD() {
  if (!bShowPrime)
    return;

  static const char *primeLabels[] = {
      "1. Visited Juvenile Sanctuary",
      "2. Hatched from Egg",
      "3. Perfect Diet",
      "4. Major Migration Zone",
      "5. 2+ Migration Zones",
      "6. 4 Patrol Zones",
      "7. Never Infertile",
      "8. No Muscle Spasms",
      "9. Raised Offspring",
      "10. Unknown Condition",
      "" // Placeholder for index 10
  };

  ImGui::SetNextWindowSize({280, 0}, ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.85f);
  ImGui::Begin("Prime Checklist", &bShowPrime,
               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);

  int metCount = 0;
  for (int i = 0; i < 10; i++)
    if (g_primeData[i])
      metCount++;

  ImVec4 themeBlue = ImVec4(0.4f, 0.6f, 1.0f, 1.0f);
  ImGui::TextColored(themeBlue, "Progress: %d/10", metCount);
  ImGui::Separator();

  for (int i = 0; i < 10; i++) {
    bool met = g_primeData[i] != 0;
    ImVec4 color = met ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f) : ImVec4(0.78f, 0.78f, 0.78f, 0.78f);
    ImGui::TextColored(color, met ? "[X] %s" : "[ ] %s", primeLabels[i]);
  }

  ImGui::Separator();
  int needed = 5 - metCount;
  if (needed <= 0)
    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "STATUS: ELIGIBLE FOR PRIME");
  else
    ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "NOT ELIGIBLE (%d more needed)", needed);

  ImGui::End();
}
} // namespace UI
