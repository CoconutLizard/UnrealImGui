// Distributed under the MIT License (MIT) (see accompanying LICENSE file)

#include "ImGuiPrivatePCH.h"

#include "ImGuiModule.h"

#include "ImGuiModuleManager.h"
#include "Utilities/WorldContext.h"
#include "Utilities/WorldContextIndex.h"

#include <IPluginManager.h>


#define LOCTEXT_NAMESPACE "FImGuiModule"


namespace CVars
{
	extern TAutoConsoleVariable<int32> InputEnabled;
	extern TAutoConsoleVariable<int32> ShowDemo;
}

struct EDelegateCategory
{
	enum
	{
		// Default per-context draw events.
		Default,

		// Multi-context draw event defined in context manager.
		MultiContext
	};
};

static FImGuiModuleManager* ModuleManager = nullptr;

#if WITH_EDITOR
FImGuiDelegateHandle FImGuiModule::AddEditorImGuiDelegate(const FImGuiDelegate& Delegate)
{
	checkf(ModuleManager, TEXT("Null pointer to internal module implementation. Is module available?"));

	return { ModuleManager->GetContextManager().GetEditorContextProxy().OnDraw().Add(Delegate),
		EDelegateCategory::Default, Utilities::EDITOR_CONTEXT_INDEX };
}
#endif

FImGuiDelegateHandle FImGuiModule::AddWorldImGuiDelegate(const FImGuiDelegate& Delegate)
{
	checkf(ModuleManager, TEXT("Null pointer to internal module implementation. Is module available?"));

#if WITH_EDITOR
	checkf(GEngine, TEXT("Null GEngine. AddWorldImGuiDelegate should be only called with GEngine initialized."));

	const FWorldContext* WorldContext = Utilities::GetWorldContext(GEngine->GameViewport);
	if (!WorldContext)
	{
		WorldContext = Utilities::GetWorldContextFromNetMode(ENetMode::NM_DedicatedServer);
	}

	checkf(WorldContext, TEXT("Couldn't find current world. AddWorldImGuiDelegate should be only called from a valid world."));

	int32 Index;
	FImGuiContextProxy& Proxy = ModuleManager->GetContextManager().GetWorldContextProxy(*WorldContext->World(), Index);
#else
	const int32 Index = Utilities::STANDALONE_GAME_CONTEXT_INDEX;
	FImGuiContextProxy& Proxy = ModuleManager->GetContextManager().GetWorldContextProxy();
#endif

	return{ Proxy.OnDraw().Add(Delegate), EDelegateCategory::Default, Index };
}

FImGuiDelegateHandle FImGuiModule::AddMultiContextImGuiDelegate(const FImGuiDelegate& Delegate)
{
	checkf(ModuleManager, TEXT("Null pointer to internal module implementation. Is module available?"));

	return { ModuleManager->GetContextManager().OnDrawMultiContext().Add(Delegate), EDelegateCategory::MultiContext };
}

void FImGuiModule::RemoveImGuiDelegate(const FImGuiDelegateHandle& Handle)
{
	if (ModuleManager)
	{
		if (Handle.Category == EDelegateCategory::MultiContext)
		{
			ModuleManager->GetContextManager().OnDrawMultiContext().Remove(Handle.Handle);
		}
		else if (auto* Proxy = ModuleManager->GetContextManager().GetContextProxy(Handle.Index))
		{
			Proxy->OnDraw().Remove(Handle.Handle);
		}
	}
}

void FImGuiModule::StartupModule()
{
	checkf(!ModuleManager, TEXT("Instance of Module Manager already exists. Instance should be created only during module startup."));

	// Create module manager that implements modules logic.
	ModuleManager = new FImGuiModuleManager();
}

void FImGuiModule::ShutdownModule()
{
	checkf(ModuleManager, TEXT("Null Module Manager. Manager instance should be deleted during module shutdown."));

	// Before we shutdown we need to delete manager that will do all necessary cleanup.
	delete ModuleManager;
	ModuleManager = nullptr;
}

bool FImGuiModule::IsInputMode() const
{
	return CVars::InputEnabled.GetValueOnAnyThread() > 0;
}

void FImGuiModule::SetInputMode(bool bEnabled)
{
	// This function is for supporting shortcut or subsitiute for console command, so we are using the same priority.
	CVars::InputEnabled->Set(bEnabled ? 1 : 0, ECVF_SetByConsole);
}

void FImGuiModule::ToggleInputMode()
{
	SetInputMode(!IsInputMode());
}

bool FImGuiModule::IsShowingDemo() const
{
	return CVars::ShowDemo.GetValueOnAnyThread() > 0;
}

void FImGuiModule::SetShowDemo(bool bShow)
{
	// This function is for supporting shortcut or subsitiute for console command, so we are using the same priority.
	CVars::ShowDemo->Set(bShow ? 1 : 0, ECVF_SetByConsole);
}

void FImGuiModule::ToggleShowDemo()
{
	SetShowDemo(!IsShowingDemo());
}

FImGuiInputState& FImGuiModule::GetInputState()
{
	return ModuleManager->Widgets[0].Pin()->InputState;
}


FImGuiModuleManager* FImGuiModule::GetModuleManager()
{
	return ModuleManager;
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FImGuiModule, ImGui)
