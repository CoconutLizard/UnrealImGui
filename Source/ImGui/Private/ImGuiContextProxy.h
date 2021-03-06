// Distributed under the MIT License (MIT) (see accompanying LICENSE file)

#pragma once

#include "ImGuiDrawData.h"

#include "GenericPlatform/ICursor.h"

#include <imgui.h>

#include <string>


class FImGuiInputState;

// Represents a single ImGui context. All the context updates should be done through this proxy. During update it
// broadcasts draw events to allow listeners draw their controls. After update it stores draw data.
class FImGuiContextProxy
{
public:

	FImGuiContextProxy(const FString& Name, FSimpleMulticastDelegate* InSharedDrawEvent);
	~FImGuiContextProxy();

	FImGuiContextProxy(const FImGuiContextProxy&) = delete;
	FImGuiContextProxy& operator=(const FImGuiContextProxy&) = delete;

	FImGuiContextProxy(FImGuiContextProxy&& Other) = default;
	FImGuiContextProxy& operator=(FImGuiContextProxy&& Other) = default;

	// Get the name of this context.
	const FString& GetName() const { return Name; }

	// Get draw data from the last frame.
	const TArray<FImGuiDrawList>& GetDrawData() const { return DrawLists; }

	// Get input state used by this context.
	const FImGuiInputState* GetInputState() const { return InputState; }

	// Set input state to be used by this context.
	void SetInputState(const FImGuiInputState* SourceInputState) { InputState = SourceInputState; }

	// If context is currently using input state to remove then remove that binding.
	void RemoveInputState(const FImGuiInputState* InputStateToRemove) { if (InputState == InputStateToRemove) InputState = nullptr; }

	// Is this context the current ImGui context.
	bool IsCurrentContext() const { return ImGui::GetCurrentContext() == Context.Get(); }

	// Set this context as current ImGui context.
	void SetAsCurrent() { ImGui::SetCurrentContext(Context.Get()); }

	bool HasActiveItem() const { return bHasActiveItem; }

	EMouseCursor::Type GetMouseCursor() const { return MouseCursor;  }

	// Delegate called right before ending the frame to allows listeners draw their controls.
	FSimpleMulticastDelegate& OnDraw() { return DrawEvent; }

	// Call draw events to allow listeners draw their widgets. Only one call per frame is processed. If it is not
	// called manually before, then it will be called from the Tick function.
	void Draw();

	// Tick to advance context to the next frame. Only one call per frame will be processed.
	void Tick(float DeltaSeconds);

private:

	void BeginFrame(float DeltaTime = 1.f / 60.f);
	void EndFrame();

	void UpdateDrawData(ImDrawData* DrawData);

	TUniquePtr<ImGuiContext> Context;

	EMouseCursor::Type MouseCursor = EMouseCursor::None;
	bool bHasActiveItem = false;

	bool bIsFrameStarted = false;
	bool bIsDrawCalled = false;

	uint32 LastFrameNumber = 0;

	FSimpleMulticastDelegate DrawEvent;
	FSimpleMulticastDelegate* SharedDrawEvent = nullptr;

	const FImGuiInputState* InputState = nullptr;

	TArray<FImGuiDrawList> DrawLists;

	FString Name;
	std::string IniFilename;
};
