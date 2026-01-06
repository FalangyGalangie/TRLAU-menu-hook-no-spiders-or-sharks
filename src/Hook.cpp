#include <MinHook.h>
#include <cstring> 
#include <Hooking.Patterns.h>
#include <string>
#include <algorithm>
#include <cctype>
#include <cstdio> 
#include <vector> // <--- ADD THIS


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
// Checks if the memory address is readable. Essential when hacking linked lists.
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

	// --- ENTITY REMOVAL LOGIC ---
	static int s_frameCounter = 0;
	s_frameCounter++;
	if (s_frameCounter < 60) return;
	s_frameCounter = 0;

	// 2. Create a "Death Row" list to store enemies we want to remove
	// We need this because we cannot delete them INSIDE the iterator loop
	// or the game will crash trying to find the 'next' item.
	std::vector<Instance*> enemiesToDelete;

	// 3. Scan the list and fill the Death Row
	Instances::Iterate([&](Instance* instance)
		{
			// Safety checks
			if (!instance || !instance->object) return;

			char* name = instance->object->name;

			if (name)
			{
				// Create lowercase copy
				char nameLower[64];
				size_t i = 0;
				while (name[i] && i < 63) {
					nameLower[i] = tolower(name[i]);
					i++;
				}
				nameLower[i] = '\0';

				// Check for sharks/spiders
				if (strstr(nameLower, "shark") ||
					strstr(nameLower, "spider") ||
					strstr(nameLower, "tarantula"))
				{
					// DO NOT DELETE HERE. Just add to the list.
					enemiesToDelete.push_back(instance);
				}
			}
		});

	// 4. NOW it is safe to delete them
	// The iterator is finished, so we aren't breaking the linked list loop.
	for (Instance* enemy : enemiesToDelete)
	{
		// Optional: Print to console
		if (enemy && enemy->object) {
			printf("!!! Deleting Enemy: %s\n", enemy->object->name);
		}

		INSTANCE_ReallyRemoveInstance(enemy, 0, false);
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