#include <chrono>
#include <thread>
#include <mutex>
#include <vector>
#include <string>
#include <unordered_map>
#include <set>
#include <cmath>
#define _USE_MATH_DEFINES
#include <d3d11.h>
#include <dxgi.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#define NOMINMAX
#include <Windows.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#include <objbase.h>
#pragma comment(lib, "ole32.lib")

#include "../DMALibrary/Memory/Memory.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "State.h"
#include "UI.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// UE Structures
struct FVector { 
    double x, y, z; 
    FVector operator+(const FVector& b) const { return { x + b.x, y + b.y, z + b.z }; }
    FVector operator-(const FVector& b) const { return { x - b.x, y - b.y, z - b.z }; }
    FVector operator*(double b) const { return { x * b, y * b, z * b }; }
};
struct FRotator { double pitch, yaw, roll; };

inline FVector GetRotatedVector(FVector v, FRotator r) {
    double p = r.pitch * M_PI / 180.0, y = r.yaw * M_PI / 180.0, r_ = r.roll * M_PI / 180.0;
    double sp = sin(p), cp = cos(p), sy = sin(y), cy = cos(y), sr = sin(r_), cr = cos(r_);
    FVector res;
    res.x = v.x * (cp * cy) + v.y * (sr * sp * cy - cr * sy) + v.z * (cr * sp * cy + sr * sy);
    res.y = v.x * (cp * sy) + v.y * (sr * sp * sy + cr * cy) + v.z * (cr * sp * sy - sr * cy);
    res.z = v.x * (-sp) + v.y * (sr * cp) + v.z * (cr * cp);
    return res;
}
template<class T> struct TArray { uintptr_t data; int count, max; };

struct EntityStats { float health = 0, maxHealth = 0, stamina = 0, maxStamina = 0, growth = 0; bool isDead = false; };
enum EntityType : uint8_t { ET_PLAYER = 0, ET_ANIMAL, ET_FISH, ET_CARCASS, ET_NEST, ET_OTHER };
static const int SPARSE_BONES = 12;
struct BoneData { FVector bones[SPARSE_BONES]; int count = 0; };
struct Entity { 
    uintptr_t actor = 0; uintptr_t rootComp = 0; uintptr_t attrSet = 0; uintptr_t boneArrData = 0; int boneArrCount = 0; EntityType type = ET_OTHER; 
    std::string name; std::string playerName = "AI"; bool bIsPlayer = false;
    bool bIsBot = false; bool bIsAdmin = false; int ping = 0; int score = 0;
    FVector pos = {}; FVector posLast = {}; FVector scale = { 1,1,1 }; FRotator rot = {}; double lastUpdateTime = 0.0; EntityStats stats = {}; bool valid = true; 
    BoneData boneData;
};
struct FastEnt { uintptr_t actor, rootComp, attrSet, boneArrData; int boneArrCount; FVector pos; EntityStats stats; EntityType type; BoneData boneData; };

// MemoryThread

// Globals
uintptr_t g_camMgr = 0, g_pc = 0, g_base = 0, g_gworld = 0, g_lastWorld = 0, g_localPawn = 0;
float g_sw = 1920.f, g_sh = 1080.f;
FVector g_camLoc = {}; FRotator g_camRot = {}; float g_camFov = 90.f;
std::unordered_map<uintptr_t, Entity> g_entities;
std::mutex g_entMutex;
std::unordered_map<uintptr_t, std::string> g_classCache;
uint8_t g_primeData[11] = {};
ImFont* g_espFont = nullptr, *g_tinyFont = nullptr;

// DirectX Globals
ID3D11Device* g_dev = nullptr;
ID3D11DeviceContext* g_ctx = nullptr;
IDXGISwapChain* g_sc = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;

// External Memory Instance
extern Memory mem;

inline bool IsValidPtr(uintptr_t p) { return p > 0x10000000 && p < 0x7FFFFFFFFFFF; }

std::string ReadFString(uintptr_t addr) {
    TArray<wchar_t> arr = mem.Read<TArray<wchar_t>>(addr);
    if (arr.count > 0 && arr.count < 128 && IsValidPtr(arr.data)) {
        std::vector<wchar_t> buf(arr.count);
        if (mem.Read(arr.data, buf.data(), arr.count * 2)) {
            std::string res; res.reserve(arr.count);
            for (auto c : buf) { if (c == 0) break; res += (c > 255) ? '?' : (char)c; }
            return res;
        }
    }
    return "";
}

// Verified UE5 GNames Resolution (The Isle Style)
std::string FNameToStr(int idx) {
    if (idx <= 0 || !IsValidPtr(g_base)) return "";
    uintptr_t pool = mem.Read<uintptr_t>(g_base + Off::GNames + (uint32_t(idx >> 16) * 8) + 16);
    if (!IsValidPtr(pool)) return "";
    uintptr_t entry = pool + uint32_t(idx & 0xFFFF) * 2;
    uint16_t hdr = mem.Read<uint16_t>(entry);
    int len = hdr >> 6; 
    if (len <= 0 || len > 255) return "";
    char buf[256]; 
    if (mem.Read(entry + 2, buf, len)) { 
        buf[len] = '\0'; 
        return std::string(buf); 
    } 
    return "";
}

std::string GetClassName(uintptr_t actor) {
    if (!IsValidPtr(actor)) return ""; 
    uintptr_t uclass = mem.Read<uintptr_t>(actor + 0x10); 
    if (!IsValidPtr(uclass)) return "";
    auto it = g_classCache.find(uclass); 
    if (it != g_classCache.end()) return it->second;
    uint32_t fid = mem.Read<uint32_t>(uclass + 0x18); 
    std::string s = FNameToStr(fid); 
    if (!s.empty()) g_classCache[uclass] = s; 
    return s;
}

EntityType Classify(const std::string& cn) {
    // Blacklist garbage / Foliage
    static const char* junk[] = { 
        "Burrow","JumpScare","MeatChunk","Plant","plant","Food","food","Water","p_fxc",
        "Tree","Bush","Agave","Fruit","Log","Rock","Stone","Grass","Flower","Vines","Cactus",
        "Pineapple","Potato","Radish","Coconut","Banana","Mango","StaticMesh","Collision",0 
    };
    for (int i = 0; junk[i]; i++) if (cn.find(junk[i]) != std::string::npos) return ET_OTHER;

    // Fish Specific
    if (cn.find("Fish") != std::string::npos || cn.find("School") != std::string::npos || cn.find("Elite") != std::string::npos) return ET_FISH;

    if (cn.find("Carcass") != std::string::npos || cn.find("Skeleton") != std::string::npos || cn.find("Body") != std::string::npos) return ET_CARCASS;
    if (cn.find("Nest") != std::string::npos || cn.find("Egg") != std::string::npos) return ET_NEST;
    
    // Players (Dinosaurs) - Use more specific strings to avoid partial matches
    static const char* p[] = { 
        "Troodon","Carnotaurus","Tyrannosaurus","Rex","Trex","Allosaurus","Stegosaurus","Deinosuchus","Tenonto","Omniraptor",
        "Hypsilophodon","Pteranodon","Beipiaosaurus","Gallimimus","Dryosaurus","Pachycephalosaurus","Diabloceratops","Herrerasaurus",
        "Dilophosaurus","Ceratosaurus","Troo","Brachi","Giraffa","Camptosaurus","Pachyrhinosaurus","Magyaro","Spinosaurus",
        "Utahraptor","Triceratops","Maiasaura","Iguanodon","Deinonychus","Puertasaurus","Shantungosaurus","Microraptor",
        "Ornithomimus","Baryonyx","Parasaurolophus","Kentrosaurus","Suchomimus","Acrocanthosaurus","Allo","Trike","Carno","Stego","Galli","Cerato",0 
    };
    for (int i = 0; p[i]; i++) if (cn.find(p[i]) != std::string::npos) return ET_PLAYER;
    
    // Animals (AI)
    static const char* a[] = { "Chicken","Boar","Rabbit","Deer","Frog","Turtle","Crab","Bird","Goat","Compsognathus","Psittacosaurus","Jungar","Ptera_AI","AI_","Hypsi_AI",0 };
    for (int i = 0; a[i]; i++) if (cn.find(a[i]) != std::string::npos) return ET_ANIMAL;
    
    return ET_OTHER;
}

std::string PrettyName(const std::string& cn) {
    struct NamePair { const char* k, * v; };
    static const NamePair pairs[] = { 
        {"Tyrannosaurus","T-Rex"},{"Rex","T-Rex"},{"Trex","T-Rex"},{"Carnotaurus","Carno"},{"Deinosuchus","Deino"},{"Allosaurus","Allo"},{"Troodon","Troodon"},{"Stegosaurus","Stego"},{"Iguanodon","Iggy"},{"Dryosaurus","Dryo"},{"Deinonychus","Deiny"},{"Hypsilophodon","Hypsi"},{"Gallimimus","Galli"},{"Puertasaurus","Puerto"},{"Shantungosaurus","Shant"},{"Tenontosaurus","Tenonto"},{"Microraptor","Micro"},{"Pteranodon","Ptero"},{"Diabloceratops","Diablo"},{"Herrerasaurus","Herrera"},{"Pachycephalosaurus","Pachy"},{"Ornithomimus","Ornitho"},{"Beipiaosaurus","Beipi"},{"Dilophosaurus","Dilo"},{"Ceratosaurus","Cerato"},{"Cerato","Cerato"},{"Baryonyx","Bary"},{"Parasaurolophus","Para"},{"Maiasaura","Maia"},{"Omniraptor","Omni"},{"Utahraptor","Utah"},{"Utah","Utah"},{"Triceratops","Trike"},{"Kentrosaurus","Kentro"},{"Brachiosaurus","Brachi"},{"Giraffatitan","Gira"},{"Suchomimus","Sucho"},{"Acrocanthosaurus","Acro"},{"Camptosaurus","Campto"},{"Psittacosaurus","Psitta"},{"Pachyrhinosaurus","Pachyrhino"},{"Compsognathus","Compy"},
        {"Carcass","Carcass"},{"Skeleton","Food Skeleton"},{"Body","Dead Body"},{"Nest","Nest"},{"Egg","Egg"},
        {"Chicken","Chicken"},{"Boar","Boar"},{"Rabbit","Rabbit"},{"Deer","Deer"},{"Frog","Frog"},
        {"Elite","Elite Fish"},{"FishSchool","Fish Pool"},{"Fish","Fish"},{nullptr,nullptr} 
    };
    for (int i = 0; pairs[i].k != nullptr; i++) if (cn.find(pairs[i].k) != std::string::npos) return pairs[i].v; 
    return cn;
}

uintptr_t GetAttrSet(uintptr_t actor) {
    if (!IsValidPtr(actor)) return 0;
    uintptr_t a = mem.Read<uintptr_t>(actor + Off::AttributeSet);
    return IsValidPtr(a) ? a : 0;
}

bool W2S(FVector w, float* sx, float* sy) {
    if (g_camFov <= 0 || g_camFov > 180) return false;
    double rP = g_camRot.pitch * (M_PI / 180.0), rY = g_camRot.yaw * (M_PI / 180.0), rR = g_camRot.roll * (M_PI / 180.0);
    double CP = cos(rP), SP = sin(rP), CY = cos(rY), SY = sin(rY), CR = cos(rR), SR = sin(rR);
    FVector F = { CP * CY,CP * SY,SP }, R = { SR * SP * CY - CR * SY,SR * SP * SY + CR * CY,-SR * CP }, U = { -(CR * SP * CY + SR * SY),CY * SR - CR * SP * SY,CR * CP };
    FVector d = { w.x - g_camLoc.x, w.y - g_camLoc.y, w.z - g_camLoc.z };
    FVector t = { d.x * R.x + d.y * R.y + d.z * R.z, d.x * U.x + d.y * U.y + d.z * U.z, d.x * F.x + d.y * F.y + d.z * F.z };
    if (t.z < 1.0) return false;
    float cx = g_sw * 0.5f, cy = g_sh * 0.5f;
    float focal = cx / tanf((float)(g_camFov * M_PI / 360.0f)) * 1.03f;
    *sx = cx + (float)(t.x * focal / t.z); *sy = cy - (float)(t.y * focal / t.z);
    return true;
}

FVector Lerp(FVector a, FVector b, double t) { t = t < 0 ? 0 : (t > 1 ? 1 : t); return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t }; }

void MemoryThread() {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    printf("[+] Memory Thread Started\n");
    auto now = std::chrono::steady_clock::now();
    auto lb = now, lp = now, lf = now, ls_play = now, ls_level = now, ls_clean = now;
    int g_distIdx = 0;
    
    while (true) {
        auto now = std::chrono::steady_clock::now();
        if (!IsValidPtr(g_base)) {
             g_base = mem.GetBaseDaddy("TheIsleClient-Win64-Shipping.exe");
             std::this_thread::sleep_for(std::chrono::seconds(1));
             continue;
        }

        uintptr_t w = mem.Read<uintptr_t>(g_base + Off::GWorld);
        if (IsValidPtr(w)) {
            if (w != g_lastWorld) { 
                { std::lock_guard<std::mutex> lk(g_entMutex); g_entities.clear(); } 
                g_lastWorld = w; g_classCache.clear(); 
                printf("[+] New World Detected: %llx\n", w);
            }
            g_gworld = w;

            // --- Fast Entity Update (10ms) ---
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lf).count() >= 10) {
                std::vector<FastEnt> fl; 
                { std::lock_guard<std::mutex> lk(g_entMutex); for (auto& p : g_entities) fl.push_back({ p.second.actor, p.second.rootComp, p.second.attrSet, p.second.boneArrData, p.second.boneArrCount, p.second.pos, p.second.stats, p.second.type, p.second.boneData }); }
                
                if (!fl.empty()) {
                    VMMDLL_SCATTER_HANDLE sc = mem.CreateScatterHandle();
                    if (sc) {
                    int added = 0;
                    for (auto& e : fl) {
                        if (!IsValidPtr(e.rootComp)) continue;
                        double dx = e.pos.x - g_camLoc.x, dy = e.pos.y - g_camLoc.y, dz = e.pos.z - g_camLoc.z, d = sqrt(dx * dx + dy * dy + dz * dz) / 100.0;
                        if (d > UI::iMaxDist + 500) continue;
                        
                        mem.AddScatterReadRequest(sc, e.rootComp + Off::RelativeLocation, &e.pos, 24);
                        if (!e.stats.isDead) {
                            if (IsValidPtr(e.attrSet) && UI::bShowBars) {
                                mem.AddScatterReadRequest(sc, e.attrSet + Off::Attr_Health, &e.stats.health, 4);
                                mem.AddScatterReadRequest(sc, e.attrSet + Off::Attr_MaxHealth, &e.stats.maxHealth, 4);
                                if (e.type == ET_PLAYER) { 
                                    mem.AddScatterReadRequest(sc, e.attrSet + Off::Attr_Stamina, &e.stats.stamina, 4); 
                                    mem.AddScatterReadRequest(sc, e.attrSet + Off::Attr_MaxStamina, &e.stats.maxStamina, 4); 
                                }
                            }
                            if (e.type == ET_PLAYER && IsValidPtr(e.actor)) mem.AddScatterReadRequest(sc, e.actor + Off::Growth, &e.stats.growth, 4);
                        }
                        if (IsValidPtr(e.actor)) {
                            if (e.type == ET_PLAYER || e.type == ET_ANIMAL) mem.AddScatterReadRequest(sc, e.actor + Off::bIsDead, &e.stats.isDead, 1);
                        }
                        // Sparse bone scatter (12 bones, integrated into fast update)
                        if (UI::bShowBones && IsValidPtr(e.boneArrData) && e.boneArrCount > 0 && d < 200.0) {
                            int step = e.boneArrCount / SPARSE_BONES;
                            if (step < 1) step = 1;
                            e.boneData.count = 0;
                            for (int bi = 0; bi < SPARSE_BONES && (bi * step) < e.boneArrCount; bi++) {
                                mem.AddScatterReadRequest(sc, e.boneArrData + ((uintptr_t)(bi * step) * 0x60) + 0x30, &e.boneData.bones[bi], sizeof(FVector));
                                e.boneData.count = bi + 1;
                            }
                        }
                        added++;
                    }
                    
                    if (added > 0) {
                        mem.ExecuteReadScatter(sc);
                    }
                    mem.CloseScatterHandle(sc);
                    }

                    { std::lock_guard<std::mutex> lk(g_entMutex); for (auto& e : fl) { 
                        auto it = g_entities.find(e.actor); 
                        if (it != g_entities.end()) { 
                            it->second.posLast = it->second.pos; it->second.pos = e.pos; it->second.stats = e.stats; 
                            if (e.boneData.count > 0) it->second.boneData = e.boneData;
                            it->second.lastUpdateTime = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
                        } 
                    } }
                }
                lf = now;
            }

            // --- Bone Pointer Cache (every 3s, resolve skelMesh -> boneArrData) ---
            static auto ls_bone = std::chrono::steady_clock::now();
            if (UI::bShowBones && std::chrono::duration_cast<std::chrono::milliseconds>(now - ls_bone).count() >= 3000) {
                std::vector<std::pair<uintptr_t, uintptr_t>> targets;
                { std::lock_guard<std::mutex> lk(g_entMutex); 
                  for (auto& p : g_entities) {
                      if ((p.second.type == ET_PLAYER || p.second.type == ET_ANIMAL) && !p.second.boneArrData)
                          targets.push_back({ p.first, p.second.rootComp });
                  }
                }
                for (auto& t : targets) {
                    uintptr_t skelMesh = mem.Read<uintptr_t>(t.first + Off::SkeletalMeshComp);
                    if (!IsValidPtr(skelMesh)) skelMesh = t.second;
                    if (!IsValidPtr(skelMesh)) continue;
                    TArray<uintptr_t> boneArr = mem.Read<TArray<uintptr_t>>(skelMesh + Off::CachedCompTransforms);
                    if (IsValidPtr(boneArr.data) && boneArr.count > 0 && boneArr.count < 200) {
                        std::lock_guard<std::mutex> lk(g_entMutex);
                        auto it = g_entities.find(t.first);
                        if (it != g_entities.end()) {
                            it->second.boneArrData = boneArr.data;
                            it->second.boneArrCount = boneArr.count;
                        }
                    }
                }
                ls_bone = now;
            }

            // --- Cleanup Pass (Stale Cache) ---
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - ls_clean).count() >= 1000) {
                std::lock_guard<std::mutex> lk(g_entMutex);
                for (auto it = g_entities.begin(); it != g_entities.end();) {
                    if (!IsValidPtr(it->second.actor)) { it = g_entities.erase(it); continue; }
                    uintptr_t root = mem.Read<uintptr_t>(it->second.actor + Off::RootComponent);
                    if (root != it->second.rootComp) { it = g_entities.erase(it); continue; }
                    ++it;
                }
                ls_clean = now;
            }

            // --- Prime Data Resolve (5s) ---
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lp).count() >= 5000) { 
                if (IsValidPtr(g_localPawn)) {
                    uint8_t temp[11] = {};
                    if (mem.Read(g_localPawn + 0x1D8C, temp, 11)) {
                        for (int i = 0; i < 11; i++) if (temp[i] > 1) temp[i] = 0; // Sanitize boolean noise
                        memcpy(g_primeData, temp, 11);
                    } else memset(g_primeData, 0, 11);
                } else memset(g_primeData, 0, 11);
                lp = now; 
            }

            // --- PlayerArray Discovery (250ms) ---
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - ls_play).count() >= 250) {
                uintptr_t gi = mem.Read<uintptr_t>(g_gworld + Off::GameInstance);
                uintptr_t lpArr = IsValidPtr(gi) ? mem.Read<uintptr_t>(gi + Off::LocalPlayers) : 0;
                uintptr_t locP = IsValidPtr(lpArr) ? mem.Read<uintptr_t>(lpArr) : 0;
                if (IsValidPtr(locP)) { 
                    uintptr_t pc = mem.Read<uintptr_t>(locP + 0x30); 
                    if (IsValidPtr(pc)) { 
                        g_pc = pc; 
                        g_camMgr = mem.Read<uintptr_t>(pc + Off::CameraManager); 
                        g_localPawn = mem.Read<uintptr_t>(pc + 0x350); 
                    } 
                }
                
                uintptr_t gs = mem.Read<uintptr_t>(g_gworld + Off::GameState);
                if (IsValidPtr(gs)) {
                    TArray<uintptr_t> pa = mem.Read<TArray<uintptr_t>>(gs + Off::PlayerArray);
                    if (IsValidPtr(pa.data) && pa.count > 0 && pa.count < 1000) {
                        std::vector<uintptr_t> states(pa.count, 0); 
                        mem.Read(pa.data, states.data(), pa.count * 8);
                        for (auto ps : states) {
                            if (!IsValidPtr(ps)) continue; 
                                uintptr_t pwn = mem.Read<uintptr_t>(ps + Off::PawnPrivate);
                                if (IsValidPtr(pwn)) {
                                    std::string pn = ReadFString(ps + Off::PlayerNamePrivate);
                                    std::string cn = GetClassName(pwn);
                                    
                                    uint8_t bits = mem.Read<uint8_t>(ps + Off::bIsBitfield);
                                    bool isBot = (bits & (1 << 3)) != 0;
                                    bool isAdmin = (bits & (1 << 1)) != 0;
                                    int ping = (int)mem.Read<uint8_t>(ps + Off::CompressedPing) * 4;
                                    int score = (int)mem.Read<float>(ps + Off::Score);

                                    {
                                        std::lock_guard<std::mutex> lk(g_entMutex);
                                        auto it = g_entities.find(pwn);
                                        bool isExisting = false;
                                        if (it != g_entities.end()) {
                                            uintptr_t root = mem.Read<uintptr_t>(pwn + Off::RootComponent);
                                            if (root != it->second.rootComp) {
                                                g_entities.erase(it); // Erase stale recycle
                                            } else {
                                                isExisting = true;
                                                it->second.bIsPlayer = true;
                                                it->second.bIsBot = isBot;
                                                it->second.bIsAdmin = isAdmin;
                                                it->second.ping = ping;
                                                it->second.score = score;
                                                if (!pn.empty()) it->second.playerName = pn;
                                            }
                                        }
                                        
                                        if (!isExisting) {
                                            EntityType t = Classify(cn);
                                            if (t != ET_OTHER) { 
                                                Entity pe; pe.actor = pwn; 
                                                pe.rootComp = mem.Read<uintptr_t>(pwn + Off::RootComponent); 
                                                pe.attrSet = GetAttrSet(pwn);
                                                pe.type = t; pe.name = cn; pe.playerName = pn.empty() ? "Player" : pn; 
                                                pe.bIsPlayer = true; pe.bIsBot = isBot; pe.bIsAdmin = isAdmin; 
                                                pe.ping = ping; pe.score = score; pe.valid = true; 
                                                g_entities[pwn] = pe;
                                            }
                                        }
                                    }
                                }
                        }
                    }
                }
                ls_play = now;
            }

            // --- Level Actor Scan (250ms) ---
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - ls_level).count() >= 250) {
                uintptr_t level = mem.Read<uintptr_t>(g_gworld + 0x30); // PersistentLevel
                if (IsValidPtr(level)) {
                    uintptr_t arr = mem.Read<uintptr_t>(level + 0xA0); 
                    int cnt = mem.Read<int>(level + 0xA8);
                    if (IsValidPtr(arr) && cnt > 0 && cnt < 100000) {
                        int chunk = 2500; 
                        if (g_distIdx >= cnt) g_distIdx = 0;
                        int end = g_distIdx + chunk; 
                        if (end > cnt) end = cnt;
                        
                        std::vector<uintptr_t> actors(end - g_distIdx); 
                        mem.Read(arr + (uintptr_t)g_distIdx * 8, actors.data(), actors.size() * 8);
                        for (auto a : actors) { 
                            if (!IsValidPtr(a)) continue;
                            
                            // Re-valid cache if address matches
                            auto it = g_entities.find(a);
                            if (it != g_entities.end()) {
                                uintptr_t root = mem.Read<uintptr_t>(a + Off::RootComponent);
                                if (root != it->second.rootComp) { 
                                    std::lock_guard<std::mutex> lk(g_entMutex); 
                                    g_entities.erase(it); 
                                } else continue; 
                            }
                            
                            std::string cn = GetClassName(a); 
                            EntityType et = Classify(cn); 
                            if (et != ET_OTHER) { 
                                uintptr_t rc = mem.Read<uintptr_t>(a + Off::RootComponent); 
                                if (!IsValidPtr(rc)) continue; 
                                FVector initPos = {}; mem.Read(rc + Off::RelativeLocation, &initPos, 24);
                                Entity e; e.actor = a; e.rootComp = rc; e.type = et; e.name = cn; e.playerName = "Player"; e.bIsPlayer = (et == ET_PLAYER); e.valid = true; e.attrSet = GetAttrSet(a);
                                e.pos = initPos; e.posLast = initPos;
                                e.lastUpdateTime = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
                                { std::lock_guard<std::mutex> lk(g_entMutex); g_entities[a] = e; } 
                            } 
                        }
                        g_distIdx = end;
                    }
                }
                ls_level = now;
            }

            // --- Lazy Attribute Resolve (every 3s, skip fish/carcass/nest) ---
            static auto ls_attr = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - ls_attr).count() >= 3000) {
                std::vector<uintptr_t> entList; 
                { std::lock_guard<std::mutex> lk(g_entMutex); for (auto& p : g_entities) {
                    if (!p.second.attrSet && p.second.type != ET_FISH && p.second.type != ET_CARCASS && p.second.type != ET_NEST)
                        entList.push_back(p.first);
                }}
                for (auto a : entList) {
                    uintptr_t found = GetAttrSet(a);
                    if (IsValidPtr(found)) { std::lock_guard<std::mutex> lk(g_entMutex); g_entities[a].attrSet = found; }
                }
                ls_attr = now;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
LRESULT WINAPI WndProc(HWND h, UINT m, WPARAM w, LPARAM l) { 
    if (ImGui_ImplWin32_WndProcHandler(h, m, w, l)) return true; 
    if (m == WM_DESTROY) PostQuitMessage(0); return DefWindowProcW(h, m, w, l); 
}

int main() {
    printf("[*] Initializing DMA...\n");
    if (!mem.Init("TheIsleClient-Win64-Shipping.exe", true, true)) {
        printf("[!] DMA Init Failed!\n");
        return 1;
    }
    
    g_base = mem.GetBaseDaddy("TheIsleClient-Win64-Shipping.exe");
    if (!g_base) {
        printf("[!] Failed to get base address!\n");
        return 1;
    }
    printf("[+] Base: %llx\n", g_base);

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0, 0, GetModuleHandle(0), 0, 0, 0, 0, L"DMALib", 0 }; 
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW, wc.lpszClassName, L"ESP", WS_POPUP, 0, 0, 1920, 1080, 0, 0, wc.hInstance, 0);
    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA); 
    MARGINS mg = { -1 }; 
    DwmExtendFrameIntoClientArea(hwnd, &mg);
    
    DXGI_SWAP_CHAIN_DESC sd = {}; sd.BufferCount = 2; sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.OutputWindow = hwnd; sd.SampleDesc.Count = 1; sd.Windowed = TRUE; sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL fl; 
    D3D11CreateDeviceAndSwapChain(0, D3D_DRIVER_TYPE_HARDWARE, 0, 0, 0, 0, D3D11_SDK_VERSION, &sd, &g_sc, &g_dev, &fl, &g_ctx);

    ID3D11Texture2D* back = nullptr;
    g_sc->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) {
        g_dev->CreateRenderTargetView(back, nullptr, &g_rtv);
        back->Release();
    }
    ShowWindow(hwnd, SW_SHOWDEFAULT); 
    
    IMGUI_CHECKVERSION(); ImGui::CreateContext(); 
    ImGuiIO& io = ImGui::GetIO();
    g_espFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arial.ttf", 14.f);
    g_tinyFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arial.ttf", 10.f); 
    
    ImGui_ImplWin32_Init(hwnd); 
    ImGui_ImplDX11_Init(g_dev, g_ctx);
    UI::SetupStyles();
    
    std::thread(MemoryThread).detach();

    static auto lastFrame = std::chrono::steady_clock::now();
    while (true) {
        MSG msg;
        while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) { 
            TranslateMessage(&msg); DispatchMessage(&msg); 
            if (msg.message == WM_QUIT) return 0; 
        }
        
        if (GetAsyncKeyState(VK_INSERT) & 1) UI::bShowMenu = !UI::bShowMenu;
        
        ImGui_ImplDX11_NewFrame(); 
        ImGui_ImplWin32_NewFrame(); 
        ImGui::NewFrame();
        
        g_sw = ImGui::GetIO().DisplaySize.x; 
        g_sh = ImGui::GetIO().DisplaySize.y;
        
        if (UI::bShowMenu) { 
            SetWindowLong(hwnd, GWL_EXSTYLE, WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW); 
            UI::RenderMenu();
        } else {
            SetWindowLong(hwnd, GWL_EXSTYLE, WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT);
        }
        
        UI::RenderPrimeHUD();

        // --- Camera Update (throttled to 5ms) ---
        {
            static auto lastCamUpdate = std::chrono::steady_clock::now();
            auto camNow = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::microseconds>(camNow - lastCamUpdate).count() >= 5000) {
                if (IsValidPtr(g_camMgr)) {
                    VMMDLL_SCATTER_HANDLE cs = mem.CreateScatterHandle();
                    if (cs) {
                        mem.AddScatterReadRequest(cs, g_camMgr + Off::CamLoc, &g_camLoc, 24);
                        mem.AddScatterReadRequest(cs, g_camMgr + Off::CamRot, &g_camRot, 24);
                        mem.AddScatterReadRequest(cs, g_camMgr + Off::CamFOV, &g_camFov, 4);
                        mem.ExecuteReadScatter(cs); mem.CloseScatterHandle(cs);
                    }
                }
                lastCamUpdate = camNow;
            }
        }

        // --- ESP Drawing ---
        ImGui::SetNextWindowPos({ 0,0 }); 
        ImGui::SetNextWindowSize({ g_sw,g_sh }); 
        ImGui::Begin("##ov", 0, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs);
        
        std::vector<Entity> snap; 
        { std::lock_guard<std::mutex> lk(g_entMutex); for (auto& it : g_entities) snap.push_back(it.second); }
        
        ImDrawList* dl = ImGui::GetWindowDrawList();
        bool adminPresent = false;

        for (auto& e : snap) {
            if (e.actor == g_localPawn) continue;
            if (e.type == ET_PLAYER && !UI::bShowPlayers) continue;
            if (e.type == ET_ANIMAL && !UI::bShowAnimals) continue;
            if (e.type == ET_FISH && !UI::bShowFish) continue;
            if (e.type == ET_CARCASS && !UI::bShowCarcass) continue;
            if (e.stats.isDead && !UI::bShowCarcass) continue;
            if (e.type == ET_NEST && !UI::bShowNests) continue;
            if (e.bIsAdmin) adminPresent = true;

            double now_s = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
            double t = (now_s - e.lastUpdateTime) / 0.010; 
            FVector renderPos = Lerp(e.posLast, e.pos, t);
            
            double dx = renderPos.x - g_camLoc.x, dy = renderPos.y - g_camLoc.y, dz = renderPos.z - g_camLoc.z;
            double d_raw = sqrt(dx * dx + dy * dy + dz * dz) / 100.0;
            if (d_raw > (double)UI::iMaxDist || d_raw < 0.1) continue;

            float sx, sy; 
            if (W2S(renderPos, &sx, &sy)) {
                ImU32 col = (e.bIsPlayer && !e.bIsBot) ? IM_COL32(255, 60, 60, 255) : (e.type == ET_ANIMAL || e.bIsBot ? IM_COL32(255, 200, 60, 255) : IM_COL32(100, 200, 255, 255));
                if (e.stats.isDead) col = IM_COL32(160, 160, 160, 200);
                if (e.bIsAdmin) col = IM_COL32(255, 0, 255, 255); 
                
                std::string spec = PrettyName(e.name);
                std::string lbl;
                if (e.stats.isDead) {
                    lbl = "Dead (" + spec + ") [" + std::to_string((int)d_raw) + "m]";
                } else {
                    std::string prefix = e.bIsBot ? "[BOT] " : "";
                    lbl = prefix + (e.bIsPlayer ? (e.playerName + " (" + spec + ")") : spec) + " [" + std::to_string((int)d_raw) + "m]";
                }
                
                auto DrawOT = [&](ImVec2 p, ImU32 c, const char* txt) { 
                    dl->AddText({ p.x + 1, p.y }, IM_COL32(0, 0, 0, 255), txt); 
                    dl->AddText({ p.x - 1, p.y }, IM_COL32(0, 0, 0, 255), txt); 
                    dl->AddText({ p.x, p.y + 1 }, IM_COL32(0, 0, 0, 255), txt); 
                    dl->AddText({ p.x, p.y - 1 }, IM_COL32(0, 0, 0, 255), txt); 
                    dl->AddText(p, c, txt); 
                };

                if (e.bIsAdmin) {
                    const char* aTxt = "ADMIN";
                    ImVec2 aSz = ImGui::CalcTextSize(aTxt);
                    DrawOT({ sx - (aSz.x / 2), sy - 15.0f }, col, aTxt);
                }

                DrawOT({ sx - (ImGui::CalcTextSize(lbl.c_str()).x / 2), sy }, col, lbl.c_str());
                
                if (!e.stats.isDead && UI::bShowBars && (e.bIsPlayer || e.type == ET_ANIMAL)) {
                    float bw = 50.0f; float bh = 2.0f;
                    float currY = sy + 15.0f;
                    
                    auto DrawBar = [&](float val, float max, ImU32 c) {
                        if (max <= 0) return;
                        float ratio = val / max;
                        if (ratio > 1.0f) ratio = 1.0f;
                        if (ratio < 0.0f) ratio = 0.0f;
                        dl->AddRectFilled({ sx - (bw / 2), currY }, { sx + (bw / 2), currY + bh }, IM_COL32(0, 0, 0, 150));
                        dl->AddRectFilled({ sx - (bw / 2), currY }, { sx - (bw / 2) + (bw * ratio), currY + bh }, c);
                        currY += bh + 2.0f;
                    };

                    DrawBar(e.stats.health, e.stats.maxHealth, IM_COL32(50, 220, 50, 220));
                    if (e.bIsPlayer) DrawBar(e.stats.stamina, e.stats.maxStamina, IM_COL32(50, 130, 255, 220));
                    
                    if (e.bIsPlayer && e.stats.growth > 0.01f) {
                        char gbuf[16]; snprintf(gbuf, 16, "G: %d%%", (int)(e.stats.growth * 100));
                        ImVec2 gsz = ImGui::CalcTextSize(gbuf);
                        DrawOT({ sx - (gsz.x / 2), currY + 2.0f }, IM_COL32(255, 255, 255, 255), gbuf);
                    }
                }

                // --- Skeleton Bones (Sparse 12-bone) ---
                if (UI::bShowBones && e.boneData.count > 2) {
                    ImVec2 screenBones[SPARSE_BONES];
                    bool boneVisible[SPARSE_BONES] = {};
                    int headIdx = 0; double maxZ = -999999;
                    
                    for (int bi = 0; bi < e.boneData.count; bi++) {
                        FVector worldB = { 
                            e.pos.x + e.boneData.bones[bi].x,
                            e.pos.y + e.boneData.bones[bi].y,
                            e.pos.z + e.boneData.bones[bi].z
                        };
                        float bx, by;
                        if (W2S(worldB, &bx, &by)) {
                            screenBones[bi] = { bx, by };
                            boneVisible[bi] = true;
                            if (worldB.z > maxZ) { maxZ = worldB.z; headIdx = bi; }
                        }
                    }
                    
                    ImU32 boneCol = IM_COL32(255, 255, 255, 140);
                    for (int bi = 1; bi < e.boneData.count; bi++) {
                        if (!boneVisible[bi] || !boneVisible[bi - 1]) continue;
                        dl->AddLine(screenBones[bi - 1], screenBones[bi], boneCol, 1.2f);
                    }
                    
                    // Head Circle
                    if (boneVisible[headIdx]) {
                        dl->AddCircle(screenBones[headIdx], 3.5f, col, 8, 1.2f);
                    }
                }
            }
        }

        if (adminPresent) {
            static float alpha = 1.0f; static bool dim = true; 
            alpha += dim ? -0.02f : 0.02f; if (alpha <= 0.3f) dim = false; if (alpha >= 1.0f) dim = true;
            const char* warn = "!!! ADMIN SPECTATING !!!";
            ImVec2 sz = ImGui::CalcTextSize(warn);
            dl->AddText(NULL, 36.0f, { (g_sw / 2) - (sz.x), 60 }, IM_COL32(255, 0, 0, (int)(alpha * 255)), warn);
        }

        // --- Tiny FPS Counter ---
        {
            static float fps = 0.0f;
            static auto lastFpsTime = std::chrono::steady_clock::now();
            static int frameCount = 0;
            frameCount++;
            auto now = std::chrono::steady_clock::now();
            float dt = std::chrono::duration<float>(now - lastFpsTime).count();
            if (dt >= 0.5f) { fps = frameCount / dt; frameCount = 0; lastFpsTime = now; }
            char fpsBuf[16]; snprintf(fpsBuf, 16, "FPS: %d", (int)fps);
            ImVec2 tsz = ImGui::CalcTextSize(fpsBuf);
            dl->AddText({ g_sw - tsz.x - 9, g_sh - tsz.y - 9 }, IM_COL32(0, 0, 0, 200), fpsBuf);
            dl->AddText({ g_sw - tsz.x - 10, g_sh - tsz.y - 10 }, IM_COL32(200, 200, 200, 255), fpsBuf);
        }

        ImGui::End(); 
        
        ImGui::Render(); 
        float clr[4] = { 0,0,0,0 }; 
        if (g_rtv) {
            g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr); 
            g_ctx->ClearRenderTargetView(g_rtv, clr); 
        }
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData()); 
        g_sc->Present(1, 0);
    } 
    return 0;
}