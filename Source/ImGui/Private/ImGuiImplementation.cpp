// Distributed under the MIT License (MIT) (see accompanying LICENSE file)

#include "ImGuiImplementation.h"

#include "ImGuiPrivatePCH.h"

// We build ImGui source code as part of this module. This is for convenience (no need to manually build libraries for
// different target platforms) but it also exposes the whole ImGui source for inspection, which can be pretty handy.
// Source files are included from Third Party directory, so we can wrap them in required by Unreal Build System headers
// without modifications in ImGui source code.
//
// We don't need to define IMGUI_API manually because it is already done for this module.

#if PLATFORM_XBOXONE
// Disable Win32 functions used in ImGui and not supported on XBox.
#define IMGUI_DISABLE_WIN32_DEFAULT_CLIPBOARD_FUNCTIONS
#define IMGUI_DISABLE_WIN32_DEFAULT_IME_FUNCTIONS
#endif // PLATFORM_XBOXONE

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#endif // PLATFORM_WINDOWS

#include "../../ThirdParty/ImGuiLibrary/Private/imgui.cpp"

//#include "imgui.cpp"
#include "../../ThirdParty/ImGuiLibrary/Private/imgui_demo.cpp"
#include "../../ThirdParty/ImGuiLibrary/Private/imgui_draw.cpp"

#if PLATFORM_WINDOWS
#include "Windows/HideWindowsPlatformTypes.h"
#endif // PLATFORM_WINDOWS


namespace ImGuiImplementation
{
	// This is exposing ImGui default context for the whole module.
	// This is assuming that we don't define custom GImGui and therefore have GImDefaultContext defined in imgui.cpp.
	ImGuiContext& GetDefaultContext()
	{
		return GImDefaultContext;
	}

	void SaveCurrentContextIniSettings(const char* Filename)
	{
		SaveIniSettingsToDisk(Filename);
	}
}