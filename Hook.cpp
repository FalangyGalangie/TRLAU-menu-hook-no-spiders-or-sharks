#include <MinHook.h>
#include <cstring> 
#include <Hooking.Patterns.h>
#include <string>
#include <algorithm>
#include <cctype>
#include <cstdio> 
#include <vector>

// Include Windows for IsBadReadPtr
#include <Windows.h> 

#include "Hook.h"
#include "Options.h"
#include "input/MessageHook.h"
#include "instance/Instances.h" // Contains INSTANCE_ReallyRemoveInstance
#include "game/Game.h"
#include "render/Font.h"
#include "game/GameLoop.h"
#include "render/DrawBatcher.h"

// Modules
#include "modules/MainMenu.h"
#include "modules/Instance.h"
#include "modules/Skew.h"
#include "modules/Render.h"
#include "modules/Log.h"
#include "modules/ScriptLog.h"
#include "modules/Level.h"
#include "modules/ModLoader.h"
#include "modules/Patches.h"
#include "modules/Frontend.h"
#include "modules/Draw.h"
#include "modules/Debug.h"
#include "modules/Materials.h"
#include "modules/camera/FreeCamera.h"

#include "cdc/render/PCDeviceManager.h"

using namespace std::placeholders;

static bool(*s_D3D_Init)();
static void* s_deviceManager;

// --- HELPER FOR SAFETY ---
// Checks if the memory address is readable. Essential when modifying live entity lists.
bool IsValidReadPtr(void* ptr, unsigned int size) {
    if (!ptr) return false;
    return !IsBadReadPtr(ptr, size);
}

static bool D3D_Init()
{
    auto ret = s_D3D_Init();
    Hook::GetInstance().OnDevice();
    return ret;
}

Hook::Hook() : m_menu(nullptr), m_modules()
{
}

void Hook::Initialize()
{
    // --- FORCE CONSOLE WINDOW OPEN ---
    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
    // ---------------------------------

    RegisterModules();

#ifndef TR8
    auto match = hook::pattern("A1 ? ? ? ? 8B 0D ? ? ? ? 68 ? ? ? ? 50 E8").count(1);
    s_deviceManager = *match.get_first<void*>(7);
#else
    auto match = hook::pattern("8B 0D ? ? ? ? 8B 01 8B 15 ? ? ? ? 8B 00 68").count(1);
    s_deviceManager = *match.get_first<void*>(2);
#endif

    MH_CreateHook(match.get_first(), D3D_Init, (void**)&s_D3D_Init);
    MH_EnableHook(MH_ALL_HOOKS);
}

void Hook::PostInitialize()
{
    m_menu = std::make_unique<Menu>();
    MessageHook::OnMessage(std::bind(&Hook::OnMessage, this, _1, _2, _3, _4));
    Font::OnFlush(std::bind(&Hook::OnFrame, this));
    GameLoop::OnLoop(std::bind(&Hook::OnLoop, this));

    for (auto& [hash, mod] : m_modules)
    {
        mod->OnPostInitialize();
    }
}

void Hook::OnMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    m_menu->OnMessage(hWnd, msg, wParam, lParam);
    for (auto& [hash, mod] : m_modules)
    {
        mod->OnInput(hWnd, msg, wParam, lParam);
    }
}

void Hook::OnFrame()
{
    for (auto& [hash, mod] : m_modules)
    {
        mod->OnFrame();
    }

#ifdef BATCH_DRAW_CALLS
    DrawBatcher::GetInstance()->Flush();
#endif
}


void Hook::OnLoop()
{
    // 1. Run existing module loops
    for (auto& [hash, mod] : m_modules) {
        mod->OnLoop();
    }

    // --- ENTITY REMOVAL LOGIC (Runs Every Frame) ---

    // Create a "Death Row" list to store enemies we want to remove
    std::vector<Instance*> enemiesToDelete;

    // Helper lambda for optimized case-insensitive substring search
    // This avoids creating new char arrays every frame
    auto contains_insensitive = [](const char* haystack, const char* needle) -> bool {
        if (!haystack || !needle) return false;
        auto it = std::search(
            haystack, haystack + strlen(haystack),
            needle, needle + strlen(needle),
            [](char ch1, char ch2) { return std::toupper(ch1) == std::toupper(ch2); }
        );
        return (it != haystack + strlen(haystack));
        };

    // Scan the list and fill the Death Row
    Instances::Iterate([&](Instance* instance)
        {
            // Safety checks
            if (!instance || !instance->object) return;

            char* name = instance->object->name;

            if (name)
            {
                // Check for keywords (case-insensitive)
                if (contains_insensitive(name, "spider") ||
                    contains_insensitive(name, "shark") ||
                    contains_insensitive(name, "tarantula"))
                {
                    // DO NOT DELETE HERE. Just add to the list.
                    enemiesToDelete.push_back(instance);
                }
            }
        });

    // Process deletions
    for (Instance* enemy : enemiesToDelete)
    {
        // Double check pointer safety immediately before action
        // This helps prevent crashes if the object was destroyed by the game engine
        // in the microseconds since we scanned the list.
        if (IsValidReadPtr(enemy, sizeof(Instance)))
        {
            // Optional: Print to console (commented out to save performance)
            // if (enemy->object) {
                printf("!!! Deleting Enemy: %s\n", enemy->object->name);
            // }

            INSTANCE_ReallyRemoveInstance(enemy, 0, false);
        }
    }
}

void Hook::OnDevice()
{
    cdc::PCDeviceManager::s_pInstance = *(cdc::PCDeviceManager**)s_deviceManager;
    PostInitialize();
}

void Hook::RegisterModules()
{
    RegisterModule<Log>();
    RegisterModule<Options>();
    RegisterModule<MainMenu>();
    RegisterModule<InstanceModule>();
    RegisterModule<Skew>();
    RegisterModule<ModLoader>();
    RegisterModule<Patches>();
    RegisterModule<FreeCamera>();
    RegisterModule<Draw>();

#ifndef TR8
    RegisterModule<LevelModule>();
    RegisterModule<Frontend>();
    RegisterModule<Render>();
    RegisterModule<Debug>();
#else
    RegisterModule<ScriptLog>();
    RegisterModule<Materials>();
#endif
}

Hook& Hook::GetInstance() noexcept
{
    static Hook instance;
    return instance;
}