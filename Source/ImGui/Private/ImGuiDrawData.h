// Distributed under the MIT License (MIT) (see accompanying LICENSE file)

#pragma once

#include "ImGuiInteroperability.h"

#include "Runtime/Launch/Resources/Version.h"
#include "Rendering/RenderingCommon.h"

#include "imgui.h"


// Starting from version 4.17 Slate doesn't have per-vertex clipping rectangle and GSlateScissorRect. Use this to
// support older engine versions.
#define WITH_OBSOLETE_CLIPPING_API		(ENGINE_MAJOR_VERSION < 4 || (ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION < 17))

// ImGui draw command data transformed for Slate.
struct FImGuiDrawCommand
{
	uint32 NumElements;
	FSlateRect ClippingRect;
	TextureIndex TextureId;
};

// Wraps raw ImGui draw list data in utilities that transform them for Slate.
class FImGuiDrawList
{
public:

	// Get the number of draw commands in this list.
	FORCEINLINE int NumCommands() const { return ImGuiCommandBuffer.Size; }

	// Get the draw command by number.
	// @param CommandNb - Number of draw command
	// @returns Draw command data
	FORCEINLINE FImGuiDrawCommand GetCommand(int CommandNb) const
	{
		const ImDrawCmd& ImGuiCommand = ImGuiCommandBuffer[CommandNb];
		return{ ImGuiCommand.ElemCount, ImGuiInterops::ToSlateRect(ImGuiCommand.ClipRect), ImGuiInterops::ToTextureIndex(ImGuiCommand.TextureId) };
	}

#if WITH_OBSOLETE_CLIPPING_API
	// Transform and copy vertex data to target buffer (old data in the target buffer are replaced).
	// @param OutVertexBuffer - Destination buffer
	// @param VertexPositionOffset - Position offset added to every vertex to transform it to different space
	// @param VertexClippingRect - Clipping rectangle for Slate vertices
	void CopyVertexData(TArray<FSlateVertex>& OutVertexBuffer, const FVector2D VertexPositionOffset, const FSlateRotatedRect& VertexClippingRect) const;
#else
	// Transform and copy vertex data to target buffer (old data in the target buffer are replaced).
	// @param OutVertexBuffer - Destination buffer
	// @param VertexPositionOffset - Position offset added to every vertex to transform it to different space
	void CopyVertexData(TArray<FSlateVertex>& OutVertexBuffer, const FVector2D VertexPositionOffset) const;
#endif // WITH_OBSOLETE_CLIPPING_API

	// Transform and copy index data to target buffer (old data in the target buffer are replaced).
	// Internal index buffer contains enough data to match the sum of NumElements from all draw commands.
	// @param OutIndexBuffer - Destination buffer
	// @param StartIndex - Start copying source data starting from this index
	// @param NumElements - How many elements we want to copy
	void CopyIndexData(TArray<SlateIndex>& OutIndexBuffer, const int32 StartIndex, const int32 NumElements) const;

	// Transfers data from ImGui source list to this object. Leaves source cleared.
	void TransferDrawData(ImDrawList& Src);

private:

	ImVector<ImDrawCmd> ImGuiCommandBuffer;
	ImVector<ImDrawIdx> ImGuiIndexBuffer;
	ImVector<ImDrawVert> ImGuiVertexBuffer;
};
