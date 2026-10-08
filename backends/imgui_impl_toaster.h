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

// struct ImGui_ImplToasterH_Frame;
// struct ImGui_ImplToasterH_Window;
//
// IMGUI_IMPL_API void ImGui_ImplToasterH_CreateOrResizeWindow(ImGui_ImplToasterH_Window *wd, uint32_t queue_family, int w, int h);
// IMGUI_IMPL_API void ImGui_ImplToasterH_DestroyWindow(ImGui_ImplToasterH_Window *wd);
//
// IMGUI_IMPL_API ImGui_ImplToasterH_Window *ImGui_ImplToasterH_GetWindowDataFromViewport(ImGuiViewport *viewport);
//
// struct ImGui_ImplToasterH_Frame
// {
// 	toaster::gpu::CommandListHandle commandList{nullptr};
// 	uint64                          timelineValue{UINT64_MAX};
// };
//
// struct ImGui_ImplToasterH_Window
// {
// 	toaster::gpu::SurfaceHandle   surface{nullptr};
// 	toaster::gpu::SwapchainHandle swapchain{nullptr};
//
// 	uint32 width{0u};
// 	uint32 height{0u};
//
// 	uint32 frameIndex{0u};
// 	uint32 maxFramesInFlight{3u};
//
// 	toaster::gpu::SemaphoreHandle timelineSemaphore{nullptr};
//
// 	ImVector<ImGui_ImplToasterH_Frame> Frames;
// 	toaster::gpu::TextureHandle        currentTexture{nullptr};
// };

#endif
