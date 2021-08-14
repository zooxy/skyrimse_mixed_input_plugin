#include "common/IDebugLog.h"
#include "common/ITypes.h"
#include "skse64/GameAPI.h"
#include "skse64/GameInput.h"
#include "skse64/PluginAPI.h"
#include "skse64_common/BranchTrampoline.h"
#include "skse64_common/SafeWrite.h"
#include "skse64_common/Utilities.h"
#include "skse64_common/skse_version.h"


#pragma comment(lib, "xinput.lib")

#include <Xinput.h>


IDebugLog gLog("skse_mixed_input_plugin.log");

PluginHandle g_pluginHandle = kPluginHandle_Invalid;

class MouseMoveEvent2 : public IDEvent, public InputEvent
{
	UInt32 keyMask;
	UInt32 x;
	UInt32 y;
};

struct InputEventHolder
{
	UInt64 unk0;
	UInt32 buttonEventIndex; // 08
	UInt32 unkC; // 0c
	UInt32 thumbstickEventIndex; // 10

	ThumbstickEvent* GetThumbstickEvents();
	ThumbstickEvent* AddThumbstickEvent(const char* controlID, UInt32 keyMask, float x, float y);
	MouseMoveEvent2* AddMouseMoveEvent(UInt32 x, UInt32 y);
	ButtonEvent* AddButtonEvent(const char* testName, float down, float time, int deviceType, UInt32 keyMask);

	static InputEventHolder* GetSingleton();

	MEMBER_FN_PREFIX(InputEventHolder);
	DEFINE_MEMBER_FN(AddThumbstickEvent_Internal, void, 0xc16ab0, UInt32 keyMask, float x, float y);
	DEFINE_MEMBER_FN(AddMouseMoveEvent_Internal, MouseMoveEvent2*, 0xc16a10, UInt32 x, UInt32 y);
	DEFINE_MEMBER_FN(AddButtonEvent_Internal, ButtonEvent*, 0xc16900, UInt32 deviceType, UInt32 keyCode, float flags, float time);
};

ThumbstickEvent* InputEventHolder::GetThumbstickEvents()
{
	return (ThumbstickEvent*)(UInt8*)this + 0x2D0;
};

MouseMoveEvent2* InputEventHolder::AddMouseMoveEvent(UInt32 x, UInt32 y)
{
	return CALL_MEMBER_FN(this, AddMouseMoveEvent_Internal)(x, y);
}

ThumbstickEvent* InputEventHolder::AddThumbstickEvent(const char* controlID, UInt32 keyMask, float x, float y)
{
	if (thumbstickEventIndex < 2)
	{
		ThumbstickEvent* event = GetThumbstickEvents() + thumbstickEventIndex;
		CALL_MEMBER_FN(this, AddThumbstickEvent_Internal)(keyMask, x, y);
		event->controlID = controlID;
		return event;
	}
	return NULL;
}

ButtonEvent* InputEventHolder::AddButtonEvent(const char* testName, float down, float time, int deviceType = kDeviceType_Keyboard, UInt32 keyMask = 0xf)
{
	ButtonEvent* buttonEvent = CALL_MEMBER_FN(this, AddButtonEvent_Internal)(deviceType, keyMask, down, time);
	if (buttonEvent)
	{
		buttonEvent->controlID = testName;
	}
	return buttonEvent;
}

InputEventHolder* InputEventHolder::GetSingleton()
{
	static RelocPtr<InputEventHolder*> g_InputEventHolder(0x2f50b28);
	return *g_InputEventHolder;
};

#if 0
static tArray<UInt32>* GetInputContextArray()
{
	return (tArray<UInt32>*)(&InputManager::GetSingleton()->unk100);
}
#endif

typedef void (*PollDeviceInputFunc)(BSInputDevice* device, float frameTime);
PollDeviceInputFunc g_Win32PollKeyboardInput;

static void PollKeyboardInput(BSInputDevice* keyboard, float frameTime)
{
	static double nextPollTime = 0;
	static double time = 0;
	static DWORD oldPacketNumber = 0;

	g_Win32PollKeyboardInput(keyboard, frameTime);
	time += frameTime;

	if (InputEventDispatcher::GetSingleton()->IsGamepadEnabled()) return;
	if (nextPollTime > time) return;

	XINPUT_STATE state;
	ZeroMemory(&state, sizeof(XINPUT_STATE));

	DWORD dwResult = XInputGetState(0, &state);

	if (dwResult == ERROR_SUCCESS) {
		short x = state.Gamepad.sThumbLX;
		short y = state.Gamepad.sThumbLY;
		bool inputChanged = oldPacketNumber != state.dwPacketNumber;
		if (inputChanged || x != 0 || y != 0) {
			float div_x = x > 0 ? 32767.0f : 32768.0f;
			float div_y = y > 0 ? 32767.0f : 32768.0f;
			InputEventHolder::GetSingleton()->AddThumbstickEvent("Move", 0xb, x / div_x, y / div_y);
		}
		nextPollTime = time;
	}
	else
	{
		// check for new controllers every 3 seconds
		nextPollTime = time + 3;
	}
	oldPacketNumber = state.dwPacketNumber;
}

typedef bool (*SteamAPI_InitFunc)();

extern "C" {

	bool SKSEPlugin_Query(const SKSEInterface* skse, PluginInfo* info)
	{
		_MESSAGE("mixed_input_plugin");

		// populate info structure
		info->infoVersion = PluginInfo::kInfoVersion;
		info->name = "mixed input plugin";
		info->version = 1;

		// store plugin handle so we can identify ourselves later
		g_pluginHandle = skse->GetPluginHandle();

		if (skse->isEditor)
		{
			_MESSAGE("loaded in editor, marking as incompatible");

			return false;
		}
		else if (skse->runtimeVersion != CURRENT_RELEASE_RUNTIME)
		{
			_MESSAGE("unsupported runtime version %08X", skse->runtimeVersion);

			return false;
		}

		// ### do not do anything else in this callback
		// ### only fill out PluginInfo and return true/false

		// supported runtime version
		return true;
	}

	bool SKSEPlugin_Load(const SKSEInterface* skse)
	{
		_MESSAGE("load mixed input plugin");

#ifdef _DEBUG
		HMODULE hSteamApi = LoadLibrary("steam_api64.dll");

		SetEnvironmentVariable("SteamGameId", "489830");
		SetEnvironmentVariable("SteamAppID", "489830");

		SteamAPI_InitFunc _SteamAPI_InitSafe = (SteamAPI_InitFunc)GetProcAddress(hSteamApi, "SteamAPI_InitSafe");

		if (!_SteamAPI_InitSafe())
		{
			_ERROR("Failed to initialize SteamAPI");
			return false;
		}
#endif

		RelocPtr<PollDeviceInputFunc> ptr(0x175e6b8);
		g_Win32PollKeyboardInput = *ptr.GetPtr();

		SafeWrite64(ptr.GetUIntPtr(), (UInt64)&PollKeyboardInput);

		return true;
	}
};


