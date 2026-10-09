#pragma once

#ifndef IMGUI_DISABLE
#include "imgui.h"

#include <toast_gpu/api.hpp>

struct ImGui_ImplToaster_PipelineInfo
{
	toaster::gpu::ESampleCount msaaSamples{toaster::gpu::ESampleCount::e1};
};

struct ImGui_ImplToaster_InitInfo
{
	toaster::gpu::ResourceDescriptorHeapHandle resourceHeap{nullptr};
	toaster::gpu::SamplerDescriptorHeapHandle  samplerHeap{nullptr};

	ImGui_ImplToaster_PipelineInfo PipelineInfoMain;
	ImGui_ImplToaster_PipelineInfo PipelineInfoForViewports;

	toaster::gpu::ShaderHandle customShaderVert{nullptr};
	toaster::gpu::ShaderHandle customShaderFrat{nullptr};

	uint32 maxFramesInFlight{3u};
	uint64 minAllocationSize{1024u * 1024u}; // 1 MiB
};

IMGUI_IMPL_API bool ImGui_ImplToaster_Init(ImGui_ImplToaster_InitInfo &info);
IMGUI_IMPL_API void ImGui_ImplToaster_Shutdown();
IMGUI_IMPL_API void ImGui_ImplToaster_NewFrame();
IMGUI_IMPL_API void ImGui_ImplToaster_RenderDrawData(ImDrawData *draw_data, toaster::gpu::CommandListHandle p_command_list);

IMGUI_IMPL_API void ImGui_ImplToaster_UpdateTexture(ImTextureData *tex);

IMGUI_IMPL_API uint32 ImGui_ImplToaster_AddTexture(toaster::gpu::TextureHandle p_texture);
IMGUI_IMPL_API void   ImGui_ImplToaster_RemoveTexture(uint32 p_texture_slot);

struct ImGui_ImplToaster_RenderState
{
	toaster::gpu::CommandListHandle commandList{nullptr};
};

#endif
