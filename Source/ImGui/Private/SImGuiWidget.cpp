// Distributed under the MIT License (MIT) (see accompanying LICENSE file)

#include "ImGuiPrivatePCH.h"

#include "SImGuiWidget.h"

#include "ImGuiContextManager.h"
#include "ImGuiContextProxy.h"
#include "ImGuiInteroperability.h"
#include "ImGuiModuleManager.h"
#include "TextureManager.h"
#include "Utilities/ScopeGuards.h"

#include "RenderingCommon.h"
#include "Rendering/ElementBatcher.h"
#include "Slate/SlateTextures.h"
#include "../SlateRHIRenderer/Private/SlateRHIRenderer.h"
#include "../SlateRHIRenderer/Private/SlateRHIResourceManager.h"

#include <Engine/Console.h>

#include <utility>

#pragma optimize ("", off)


// High enough z-order guarantees that ImGui output is rendered on top of the game UI.
constexpr int32 IMGUI_WIDGET_Z_ORDER = 10000;


DEFINE_LOG_CATEGORY_STATIC(LogImGuiWidget, Warning, All);

#define TEXT_INPUT_MODE(Val) (\
	(Val) == EInputMode::MouseAndKeyboard ? TEXT("MouseAndKeyboard") :\
	(Val) == EInputMode::MousePointerOnly ? TEXT("MousePointerOnly") :\
	TEXT("None"))

#define TEXT_BOOL(Val) ((Val) ? TEXT("true") : TEXT("false"))


namespace CVars
{
	TAutoConsoleVariable<int> InputEnabled(TEXT("ImGui.InputEnabled"), 0,
		TEXT("Enable or disable ImGui input mode.\n")
		TEXT("0: disabled (default)\n")
		TEXT("1: enabled, input is routed to ImGui and with a few exceptions is consumed"),
		ECVF_Default);

	TAutoConsoleVariable<int> DrawMouseCursor(TEXT("ImGui.DrawMouseCursor"), 0,
		TEXT("Whether or not mouse cursor in input mode should be drawn by ImGui.\n")
		TEXT("0: disabled, hardware cursor will be used (default)\n")
		TEXT("1: enabled, ImGui will take care for drawing mouse cursor"),
		ECVF_Default);

	TAutoConsoleVariable<int> DebugWidget(TEXT("ImGui.Debug.Widget"), 0,
		TEXT("Show debug for SImGuiWidget.\n")
		TEXT("0: disabled (default)\n")
		TEXT("1: enabled"),
		ECVF_Default);

	TAutoConsoleVariable<int> DebugInput(TEXT("ImGui.Debug.Input"), 0,
		TEXT("Show debug for input state.\n")
		TEXT("0: disabled (default)\n")
		TEXT("1: enabled"),
		ECVF_Default);
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SImGuiWidget::Construct(const FArguments& InArgs)
{
	checkf(InArgs._ModuleManager, TEXT("Null Module Manager argument"));
	checkf(InArgs._GameViewport, TEXT("Null Game Viewport argument"));

	ModuleManager = InArgs._ModuleManager;
	if (InArgs._GameViewport)
	{
		float pHere = 0.0f;
	}
	GameViewport = InArgs._GameViewport;
	ContextIndex = InArgs._ContextIndex;

	// NOTE: We could allow null game viewports (for instance to attach to non-viewport widgets) but we would need
	// to modify a few functions that assume valid viewport pointer.
	GameViewport->AddViewportWidgetContent(SharedThis(this), IMGUI_WIDGET_Z_ORDER);

	// Disable mouse cursor over this widget as we will use ImGui to draw it.
	SetCursor(EMouseCursor::None);

	// Sync visibility with default input enabled state.
	SetVisibilityFromInputEnabled();

	// Register to get post-update notifications, so we can clean frame updates.
	ModuleManager->OnPostImGuiUpdate().AddRaw(this, &SImGuiWidget::OnPostImGuiUpdate);

	// Bind this widget to its context proxy.
	auto* ContextProxy = ModuleManager->GetContextManager().GetContextProxy(ContextIndex);
	checkf(ContextProxy, TEXT("Missing context during widget construction: ContextIndex = %d"), ContextIndex);
	ContextProxy->OnDraw().AddRaw(this, &SImGuiWidget::OnDebugDraw);
	ContextProxy->SetInputState(&InputState);

	FSlateApplication& SlateApp = FSlateApplication::Get();
	TSharedPtr<FSlateRenderer> SlateRenderer = SlateApp.Renderer;

	FSlateRenderer* RawRenderer = SlateRenderer.Get();

	SlateRHIRenderer = TSharedPtr<FSlateRHIRenderer>(static_cast<FSlateRHIRenderer*>(RawRenderer));
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

SImGuiWidget::~SImGuiWidget()
{
	// Remove binding between this widget and its context proxy.
	if (auto* ContextProxy = ModuleManager->GetContextManager().GetContextProxy(ContextIndex))
	{
		ContextProxy->OnDraw().RemoveAll(this);
		ContextProxy->RemoveInputState(&InputState);
	}

	// Unregister from post-update notifications.
	ModuleManager->OnPostImGuiUpdate().RemoveAll(this);
}

void SImGuiWidget::Detach()
{
	if (GameViewport.IsValid())
	{
		GameViewport->RemoveViewportWidgetContent(SharedThis(this));
		GameViewport.Reset();
	}
}

void SImGuiWidget::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	Super::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	UpdateMouseStatus();

	// Note: Moving that update to console variable sink or callback might seem like a better alternative but input
	// setup in this function is better handled here.
	UpdateInputEnabled();
}

FReply SImGuiWidget::OnKeyChar(const FGeometry& MyGeometry, const FCharacterEvent& CharacterEvent)
{
	if (IsConsoleOpened())
	{
		return FReply::Unhandled();
	}

	InputState.AddCharacter(CharacterEvent.GetCharacter());

	return FReply::Handled();
}

FReply SImGuiWidget::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& KeyEvent)
{
	if (IsConsoleOpened() || IgnoreKeyEvent(KeyEvent))
	{
		return FReply::Unhandled();
	}

	InputState.SetKeyDown(ImGuiInterops::GetKeyIndex(KeyEvent), true);
	CopyModifierKeys(KeyEvent);

	// If this is tilde key then let input through and release the focus to allow console to process it.
	if (KeyEvent.GetKey() == EKeys::Tilde)
	{
		return FReply::Unhandled();
	}

	return FReply::Handled();
}

FReply SImGuiWidget::OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& KeyEvent)
{
	// Even if we don't send new keystrokes to ImGui, we still handle key up events, to make sure that we clear keys
	// pressed before suppressing keyboard input.
	InputState.SetKeyDown(ImGuiInterops::GetKeyIndex(KeyEvent), false);
	CopyModifierKeys(KeyEvent);

	// If console is opened we notify key change but we also let event trough, so it can be handled by console.
	return IsConsoleOpened() ? FReply::Unhandled() : FReply::Handled();
}

FReply SImGuiWidget::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	InputState.SetMouseDown(ImGuiInterops::GetMouseIndex(MouseEvent), true);
	CopyModifierKeys(MouseEvent);

	return FReply::Handled();
}

FReply SImGuiWidget::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	InputState.SetMouseDown(ImGuiInterops::GetMouseIndex(MouseEvent), true);
	CopyModifierKeys(MouseEvent);

	return FReply::Handled();
}

FReply SImGuiWidget::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	InputState.SetMouseDown(ImGuiInterops::GetMouseIndex(MouseEvent), false);
	CopyModifierKeys(MouseEvent);

	return FReply::Handled();
}

FReply SImGuiWidget::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	InputState.AddMouseWheelDelta(MouseEvent.GetWheelDelta());
	CopyModifierKeys(MouseEvent);

	return FReply::Handled();
}

FCursorReply SImGuiWidget::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	EMouseCursor::Type MouseCursor = EMouseCursor::None;
	if (CVars::DrawMouseCursor.GetValueOnGameThread() <= 0)
	{
		if (FImGuiContextProxy* ContextProxy = ModuleManager->GetContextManager().GetContextProxy(ContextIndex))
		{
			MouseCursor = ContextProxy->GetMouseCursor();
		}
	}

	return FCursorReply::Cursor(MouseCursor);
}

FReply SImGuiWidget::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	InputState.SetMousePosition(MouseEvent.GetScreenSpacePosition() - MyGeometry.AbsolutePosition);
	CopyModifierKeys(MouseEvent);

	// This event is called in every frame when we have a mouse, so we can use it to raise notifications.
	NotifyMouseEvent();

	return FReply::Handled();
}

FReply SImGuiWidget::OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& FocusEvent)
{
	Super::OnFocusReceived(MyGeometry, FocusEvent);

	UE_LOG(LogImGuiWidget, VeryVerbose, TEXT("ImGui Widget %d - Focus Received."), ContextIndex);

	// If widget has a keyboard focus we always maintain mouse input. Technically, if mouse is outside of the widget
	// area it won't generate events but we freeze its state until it either comes back or input is completely lost.
	UpdateInputMode(true, IsDirectlyHovered());

	FSlateApplication::Get().ResetToDefaultPointerInputSettings();
	return FReply::Handled();
}

void SImGuiWidget::OnFocusLost(const FFocusEvent& FocusEvent)
{
	Super::OnFocusLost(FocusEvent);

	UE_LOG(LogImGuiWidget, VeryVerbose, TEXT("ImGui Widget %d - Focus Lost."), ContextIndex);

	UpdateInputMode(false, IsDirectlyHovered());
}

void SImGuiWidget::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	Super::OnMouseEnter(MyGeometry, MouseEvent);

	UE_LOG(LogImGuiWidget, VeryVerbose, TEXT("ImGui Widget %d - Mouse Enter."), ContextIndex);

	// If mouse enters while input is active then we need to update mouse buttons because there is a chance that we
	// missed some events.
	if (InputMode != EInputMode::None)
	{
		for (const FKey& Button : { EKeys::LeftMouseButton, EKeys::MiddleMouseButton, EKeys::RightMouseButton, EKeys::ThumbMouseButton, EKeys::ThumbMouseButton2 })
		{
			InputState.SetMouseDown(ImGuiInterops::GetMouseIndex(Button), MouseEvent.IsMouseButtonDown(Button));
		}
	}

	UpdateInputMode(HasKeyboardFocus(), true);
}

void SImGuiWidget::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	Super::OnMouseLeave(MouseEvent);

	UE_LOG(LogImGuiWidget, VeryVerbose, TEXT("ImGui Widget %d - Mouse Leave."), ContextIndex);

	// We don't get any events when application loses focus, but often this is followed by OnMouseLeave, so we can use
	// this event to immediately disable keyboard input if application lost focus.
	UpdateInputMode(HasKeyboardFocus() && GameViewport->Viewport->IsForegroundWindow(), false);
}

void SImGuiWidget::CopyModifierKeys(const FInputEvent& InputEvent)
{
	InputState.SetControlDown(InputEvent.IsControlDown());
	InputState.SetShiftDown(InputEvent.IsShiftDown());
	InputState.SetAltDown(InputEvent.IsAltDown());
}

void SImGuiWidget::CopyModifierKeys(const FPointerEvent& MouseEvent)
{
	if (InputMode == EInputMode::MousePointerOnly)
	{
		CopyModifierKeys(static_cast<const FInputEvent&>(MouseEvent));
	}
}

bool SImGuiWidget::IsConsoleOpened() const
{
	return GameViewport->ViewportConsole && GameViewport->ViewportConsole->ConsoleState != NAME_None;
}

bool SImGuiWidget::IgnoreKeyEvent(const FKeyEvent& KeyEvent) const
{
	// Ignore console open/close events.
	if (KeyEvent.GetKey() == EKeys::Tilde)
	{
		return true;
	}

	// Ignore escape keys unless they are needed to cancel operations in ImGui.
	if (KeyEvent.GetKey() == EKeys::Escape)
	{
		auto* ContextProxy = ModuleManager->GetContextManager().GetContextProxy(ContextIndex);
		if (!ContextProxy || !ContextProxy->HasActiveItem())
		{
			return true;
		}
	}

	return false;
}

void SImGuiWidget::SetVisibilityFromInputEnabled()
{
	// If we don't use input disable hit test to make this widget invisible for cursors hit detection.
	SetVisibility(bInputEnabled ? EVisibility::Visible : EVisibility::HitTestInvisible);

	UE_LOG(LogImGuiWidget, VeryVerbose, TEXT("ImGui Widget %d - Visibility updated to '%s'."),
		ContextIndex, *GetVisibility().ToString());
}

void SImGuiWidget::UpdateInputEnabled()
{
	const bool bEnabled = CVars::InputEnabled.GetValueOnGameThread() > 0;
	if (bInputEnabled != bEnabled)
	{
		bInputEnabled = bEnabled;

		UE_LOG(LogImGuiWidget, Log, TEXT("ImGui Widget %d - Input Enabled changed to '%s'."),
			ContextIndex, TEXT_BOOL(bInputEnabled));

		SetVisibilityFromInputEnabled();

		if (!bInputEnabled)
		{
			auto& Slate = FSlateApplication::Get();
			if (Slate.GetKeyboardFocusedWidget().Get() == this)
			{
				Slate.ResetToDefaultPointerInputSettings();
				//Slate.SetUserFocus(Slate.GetUserIndexForKeyboard(),
				//	PreviousUserFocusedWidget.IsValid() ? PreviousUserFocusedWidget.Pin() : GameViewport->GetGameViewportWidget());
			}

			PreviousUserFocusedWidget.Reset();

			UpdateInputMode(false, false);
		}
	}

	// Note: Some widgets, like console, can reset focus to viewport after we already grabbed it. If we detect that
	// viewport has a focus while input is enabled we will take it.
	if (bInputEnabled && !HasKeyboardFocus() && !IsConsoleOpened())
	{
		const auto& ViewportWidget = GameViewport->GetGameViewportWidget();
		if (ViewportWidget->HasKeyboardFocus() || ViewportWidget->HasFocusedDescendants())
		{
			auto& Slate = FSlateApplication::Get();
			//PreviousUserFocusedWidget = Slate.GetUserFocusedWidget(Slate.GetUserIndexForKeyboard());
			Slate.SetKeyboardFocus(SharedThis(this));
		}
	}

	// We don't get any events when application loses focus (we get OnMouseLeave but not always) but we fix it with
	// this manual check. We still allow the above code to run, even if we need to suppress keyboard input right after
	// that.
	if (bInputEnabled && !GameViewport->Viewport->IsForegroundWindow() && InputMode == EInputMode::MouseAndKeyboard)
	{
		UpdateInputMode(false, IsDirectlyHovered());
	}
}

void SImGuiWidget::UpdateInputMode(bool bHasKeyboardFocus, bool bHasMousePointer)
{
	const EInputMode NewInputMode =
		bHasKeyboardFocus ? EInputMode::MouseAndKeyboard :
		bHasMousePointer ? EInputMode::MousePointerOnly :
		EInputMode::None;

	if (InputMode != NewInputMode)
	{
		UE_LOG(LogImGuiWidget, Verbose, TEXT("ImGui Widget %d - Input Mode changed from '%s' to '%s'."),
			ContextIndex, TEXT_INPUT_MODE(InputMode), TEXT_INPUT_MODE(NewInputMode));

		// We need to reset input components if we are either fully shutting down or we are downgrading from full to
		// mouse-only input mode.
		if (NewInputMode == EInputMode::None)
		{
			InputState.ResetState();
		}
		else if (InputMode == EInputMode::MouseAndKeyboard)
		{
			InputState.ResetKeyboardState();
		}

		InputMode = NewInputMode;

		ClearMouseEventNotification();
	}

	InputState.SetMousePointer(bHasMousePointer && CVars::DrawMouseCursor.GetValueOnGameThread() > 0);
}

void SImGuiWidget::UpdateMouseStatus()
{
	// Note: Mouse leave events can get lost if other viewport takes mouse capture (for instance console is opened by
	// different viewport when this widget is hovered). With that we lose a chance to cleanup and hide ImGui pointer.
	// We could either update ImGui pointer in every frame or like below, use mouse events to catch when mouse is lost.

	if (InputMode == EInputMode::MousePointerOnly)
	{
		if (!HasMouseEventNotification())
		{
			UpdateInputMode(false, IsDirectlyHovered());
		}
		ClearMouseEventNotification();
	}
}

void SImGuiWidget::OnPostImGuiUpdate()
{
	if (InputMode != EInputMode::None)
	{
		InputState.ClearUpdateState();
	}
}

int32 SImGuiWidget::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyClippingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& WidgetStyle, bool bParentEnabled) const
{
	if (FImGuiContextProxy* ContextProxy = ModuleManager->GetContextManager().GetContextProxy(ContextIndex))
	{
		// Manually update ImGui context to minimise lag between creating and rendering ImGui output. This will also
		// keep frame tearing at minimum because it is executed at the very end of the frame.
		ContextProxy->Tick(FSlateApplication::Get().GetDeltaTime());

		// Calculate offset that will transform vertex positions to screen space - rounded to avoid half pixel offsets.
		const FVector2D VertexPositionOffset{ FMath::RoundToFloat(MyClippingRect.Left), FMath::RoundToFloat(MyClippingRect.Top) };

		// Convert clipping rectangle to format required by Slate vertex.
		const FSlateRotatedRect VertexClippingRect{ MyClippingRect };

		for (const auto& DrawList : ContextProxy->GetDrawData())
		{
#if WITH_OBSOLETE_CLIPPING_API
			DrawList.CopyVertexData(VertexBuffer, VertexPositionOffset, VertexClippingRect);

			// Get access to the Slate scissor rectangle defined in Slate Core API, so we can customize elements drawing.
			extern SLATECORE_API TOptional<FShortRect> GSlateScissorRect;
			auto GSlateScissorRectSaver = ScopeGuards::MakeStateSaver(GSlateScissorRect);
#else
			DrawList.CopyVertexData(VertexBuffer, VertexPositionOffset);
#endif // WITH_OBSOLETE_CLIPPING_API

			int IndexBufferOffset = 0;
			for (int CommandNb = 0; CommandNb < DrawList.NumCommands(); CommandNb++)
			{
				const auto& DrawCommand = DrawList.GetCommand(CommandNb);

				DrawList.CopyIndexData(IndexBuffer, IndexBufferOffset, DrawCommand.NumElements);

				// Advance offset by number of copied elements to position it for the next command.
				IndexBufferOffset += DrawCommand.NumElements;

				// Get texture resource handle for this draw command (null index will be also mapped to a valid texture).
				const FSlateBrush& Brush = ModuleManager->GetTextureManager().GetBrush(DrawCommand.TextureId);

				// Transform clipping rectangle to screen space and apply to elements that we draw.
				const FSlateRect ClippingRect = DrawCommand.ClippingRect.OffsetBy(MyClippingRect.GetTopLeft()).IntersectionWith(MyClippingRect);

#if WITH_OBSOLETE_CLIPPING_API
				GSlateScissorRect = FShortRect{ ClippingRect };
#else
				OutDrawElements.PushClip(FSlateClippingZone{ ClippingRect });
#endif // WITH_OBSOLETE_CLIPPING_API

				TSharedPtr<FSlateElementBatcher> Batcher = SlateRHIRenderer->GetElementBatcher();

				Batcher->BatchData = &OutDrawElements.GetBatchData();

				if (Batcher->BatchData)
				{
					FElementBatchMap LayerToElementDefault;
					FElementBatchMap& LayerToElementBatches = Batcher->BatchData ? Batcher->BatchData->GetElementBatchMap() : LayerToElementDefault;

					if (VertexBuffer.Num() > 0)
					{
						// See if the layer already exists.
						FElementBatchArray* ElementBatches = LayerToElementBatches.Find(LayerId);
						if (!ElementBatches)
						{
							// The layer doesn't exist so make it now
							ElementBatches = &LayerToElementBatches.Add(LayerId);
						}
						check(ElementBatches);

						FSlateElementBatch NewBatch(
							SlateRHIRenderer->ResourceManager->GetShaderResource(Brush)->Resource,
							FShaderParams(),
							ESlateShader::Default,
							ESlateDrawPrimitive::TriangleList,
							ESlateDrawEffect::None,
							ESlateBatchDrawFlag::None,
							TOptional<FShortRect>()
						);

						int32 Index = ElementBatches->Add(NewBatch);
						FSlateElementBatch* ElementBatch = &(*ElementBatches)[Index];

						Batcher->BatchData->AssignVertexArrayToBatch(*ElementBatch);
						Batcher->BatchData->AssignIndexArrayToBatch(*ElementBatch);

						TArray<FSlateVertex>& BatchVertices = Batcher->BatchData->GetBatchVertexList(*ElementBatch);
						TArray<SlateIndex>& BatchIndices = Batcher->BatchData->GetBatchIndexList(*ElementBatch);

						// Vertex Buffer since  it is already in slate format it is a straight copy
						BatchVertices = VertexBuffer;
						BatchIndices = IndexBuffer;
					}
				}

#if !WITH_OBSOLETE_CLIPPING_API
				OutDrawElements.PopClip();
#endif // WITH_OBSOLETE_CLIPPING_API
			}
		}
	}

	return LayerId;
}

FVector2D SImGuiWidget::ComputeDesiredSize(float) const
{
	return FVector2D{ 3840.f, 2160.f };
}

static TArray<FKey> GetImGuiMappedKeys()
{
	TArray<FKey> Keys;
	Keys.Reserve(Utilities::ArraySize<ImGuiInterops::ImGuiTypes::FKeyMap>::value + 8);

	// ImGui IO key map.
	Keys.Emplace(EKeys::Tab);
	Keys.Emplace(EKeys::Left);
	Keys.Emplace(EKeys::Right);
	Keys.Emplace(EKeys::Up);
	Keys.Emplace(EKeys::Down);
	Keys.Emplace(EKeys::PageUp);
	Keys.Emplace(EKeys::PageDown);
	Keys.Emplace(EKeys::Home);
	Keys.Emplace(EKeys::End);
	Keys.Emplace(EKeys::Delete);
	Keys.Emplace(EKeys::BackSpace);
	Keys.Emplace(EKeys::Enter);
	Keys.Emplace(EKeys::Escape);
	Keys.Emplace(EKeys::A);
	Keys.Emplace(EKeys::C);
	Keys.Emplace(EKeys::V);
	Keys.Emplace(EKeys::X);
	Keys.Emplace(EKeys::Y);
	Keys.Emplace(EKeys::Z);

	// Modifier keys.
	Keys.Emplace(EKeys::LeftShift);
	Keys.Emplace(EKeys::RightShift);
	Keys.Emplace(EKeys::LeftControl);
	Keys.Emplace(EKeys::RightControl);
	Keys.Emplace(EKeys::LeftAlt);
	Keys.Emplace(EKeys::RightAlt);
	Keys.Emplace(EKeys::LeftCommand);
	Keys.Emplace(EKeys::RightCommand);

	return Keys;
}

// Column layout unitlities.
namespace Columns
{
	template<typename FunctorType>
	static void CollapsingGroup(const char* Name, int Columns, FunctorType&& DrawContent)
	{
		if (ImGui::CollapsingHeader(Name, ImGuiTreeNodeFlags_DefaultOpen))
		{
			const int LastColumns = ImGui::GetColumnsCount();
			ImGui::Columns(Columns, nullptr, false);
			DrawContent();
			ImGui::Columns(LastColumns);
		}
	}
}

// Controls tweaked for 2-columns layout.
namespace TwoColumns
{
	template<typename FunctorType>
	static inline void CollapsingGroup(const char* Name, FunctorType&& DrawContent)
	{
		Columns::CollapsingGroup(Name, 2, std::forward<FunctorType>(DrawContent));
	}

	namespace
	{
		void LabelText(const char* Label)
		{
			ImGui::Text("%s:", Label);
		}

		void LabelText(const wchar_t* Label)
		{
			ImGui::Text("%ls:", Label);
		}
	}

	template<typename LabelType>
	static void Value(LabelType&& Label, int32 Value)
	{
		LabelText(Label); ImGui::NextColumn();
		ImGui::Text("%d", Value); ImGui::NextColumn();
	}

	template<typename LabelType>
	static void Value(LabelType&& Label, uint32 Value)
	{
		LabelText(Label); ImGui::NextColumn();
		ImGui::Text("%u", Value); ImGui::NextColumn();
	}

	template<typename LabelType>
	static void Value(LabelType&& Label, float Value)
	{
		LabelText(Label); ImGui::NextColumn();
		ImGui::Text("%f", Value); ImGui::NextColumn();
	}

	template<typename LabelType>
	static void Value(LabelType&& Label, bool bValue)
	{
		LabelText(Label); ImGui::NextColumn();
		ImGui::Text("%ls", TEXT_BOOL(bValue)); ImGui::NextColumn();
	}

	template<typename LabelType>
	static void Value(LabelType&& Label, const TCHAR* Value)
	{
		LabelText(Label); ImGui::NextColumn();
		ImGui::Text("%ls", Value); ImGui::NextColumn();
	}
}

namespace Styles
{
	template<typename FunctorType>
	static void TextHighlight(bool bHighlight, FunctorType&& DrawContent)
	{
		if (bHighlight)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, { 1.f, 1.f, 0.5f, 1.f });
		}
		DrawContent();
		if (bHighlight)
		{
			ImGui::PopStyleColor();
		}
	}
}

void SImGuiWidget::OnDebugDraw()
{
	if (CVars::DebugWidget.GetValueOnGameThread() > 0)
	{
		bool bDebug = true;
		ImGui::SetNextWindowSize(ImVec2(380, 480), ImGuiSetCond_Once);
		if (ImGui::Begin("ImGui Widget Debug", &bDebug))
		{
			ImGui::Spacing();

			TwoColumns::CollapsingGroup("Context", [&]()
			{
				TwoColumns::Value("Context Index", ContextIndex);
				FImGuiContextProxy* ContextProxy = ModuleManager->GetContextManager().GetContextProxy(ContextIndex);
				TwoColumns::Value("Context Name", ContextProxy ? *ContextProxy->GetName() : TEXT("< Null >"));
				TwoColumns::Value("Game Viewport", *GameViewport->GetName());
			});

			TwoColumns::CollapsingGroup("Input Mode", [&]()
			{
				TwoColumns::Value("Input Enabled", bInputEnabled);
				TwoColumns::Value("Input Mode", TEXT_INPUT_MODE(InputMode));
				TwoColumns::Value("Input Has Mouse Pointer", InputState.HasMousePointer());
			});

			TwoColumns::CollapsingGroup("Widget", [&]()
			{
				TwoColumns::Value("Visibility", *GetVisibility().ToString());
				TwoColumns::Value("Is Hovered", IsHovered());
				TwoColumns::Value("Is Directly Hovered", IsDirectlyHovered());
				TwoColumns::Value("Has Keyboard Input", HasKeyboardFocus());
			});

			TwoColumns::CollapsingGroup("Viewport", [&]()
			{
				const auto& ViewportWidget = GameViewport->GetGameViewportWidget();
				TwoColumns::Value("Is Foreground Window", GameViewport->Viewport->IsForegroundWindow());
				TwoColumns::Value("Is Hovered", ViewportWidget->IsHovered());
				TwoColumns::Value("Is Directly Hovered", ViewportWidget->IsDirectlyHovered());
				TwoColumns::Value("Has Mouse Capture", ViewportWidget->HasMouseCapture());
				TwoColumns::Value("Has Keyboard Input", ViewportWidget->HasKeyboardFocus());
				TwoColumns::Value("Has Focused Descendants", ViewportWidget->HasFocusedDescendants());
				auto Widget = PreviousUserFocusedWidget.Pin();
				TwoColumns::Value("Previous User Focused", Widget.IsValid() ? *Widget->GetTypeAsString() : TEXT("None"));
			});
		}
		ImGui::End();

		if (!bDebug)
		{
			CVars::DebugWidget.AsVariable()->Set(0, ECVF_SetByConsole);
		}
	}

	if (CVars::DebugInput.GetValueOnGameThread() > 0)
	{
		bool bDebug = true;
		ImGui::SetNextWindowSize(ImVec2(460, 480), ImGuiSetCond_Once);
		if (ImGui::Begin("ImGui Input State", &bDebug))
		{
			const ImVec4 HiglightColor{ 1.f, 1.f, 0.5f, 1.f };
			Columns::CollapsingGroup("Mapped Keys", 4, [&]()
			{
				static const auto& Keys = GetImGuiMappedKeys();

				const int32 Num = Keys.Num();

				// Simplified when slicing for two 2.
				const int32 RowsNum = (Num + 1) / 2;

				for (int32 Row = 0; Row < RowsNum; Row++)
				{
					for (int32 Col = 0; Col < 2; Col++)
					{
						const int32 Idx = Row + Col * RowsNum;
						if (Idx < Num)
						{
							const FKey& Key = Keys[Idx];
							const uint32 KeyIndex = ImGuiInterops::GetKeyIndex(Key);
							Styles::TextHighlight(InputState.GetKeys()[KeyIndex], [&]()
							{
								TwoColumns::Value(*Key.GetDisplayName().ToString(), KeyIndex);
							});
						}
						else
						{
							ImGui::NextColumn(); ImGui::NextColumn();
						}
					}
				}
			});

			Columns::CollapsingGroup("Modifier Keys", 4, [&]()
			{
				Styles::TextHighlight(InputState.IsShiftDown(), [&]() { ImGui::Text("Shift"); }); ImGui::NextColumn();
				Styles::TextHighlight(InputState.IsControlDown(), [&]() { ImGui::Text("Control"); }); ImGui::NextColumn();
				Styles::TextHighlight(InputState.IsAltDown(), [&]() { ImGui::Text("Alt"); }); ImGui::NextColumn();
				ImGui::NextColumn();
			});

			Columns::CollapsingGroup("Mouse Buttons", 4, [&]()
			{
				static const FKey Buttons[] = { EKeys::LeftMouseButton, EKeys::RightMouseButton,
					EKeys::MiddleMouseButton, EKeys::ThumbMouseButton, EKeys::ThumbMouseButton2 };

				const int32 Num = Utilities::GetArraySize(Buttons);

				// Simplified when slicing for two 2.
				const int32 RowsNum = (Num + 1) / 2;

				for (int32 Row = 0; Row < RowsNum; Row++)
				{
					for (int32 Col = 0; Col < 2; Col++)
					{
						const int32 Idx = Row + Col * RowsNum;
						if (Idx < Num)
						{
							const FKey& Button = Buttons[Idx];
							const uint32 MouseIndex = ImGuiInterops::GetMouseIndex(Button);
							Styles::TextHighlight(InputState.GetMouseButtons()[MouseIndex], [&]()
							{
								TwoColumns::Value(*Button.GetDisplayName().ToString(), MouseIndex);
							});
						}
						else
						{
							ImGui::NextColumn(); ImGui::NextColumn();
						}
					}
				}
			});

			Columns::CollapsingGroup("Mouse Axes", 4, [&]()
			{
				TwoColumns::Value("Position X", InputState.GetMousePosition().X);
				TwoColumns::Value("Position Y", InputState.GetMousePosition().Y);
				TwoColumns::Value("Wheel Delta", InputState.GetMouseWheelDelta());
				ImGui::NextColumn(); ImGui::NextColumn();
			});

			if (!bDebug)
			{
				CVars::DebugInput.AsVariable()->Set(0, ECVF_SetByConsole);
			}
		}
		ImGui::End();
	}
}

#undef TEXT_INPUT_MODE
#undef TEXT_BOOL

#pragma optimize ("", on)
