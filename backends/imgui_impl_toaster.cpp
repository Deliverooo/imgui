#ifndef IMGUI_DISABLE

#include "imgui_impl_toaster.h"
#include <toast_gpu/upload.hpp>

#include <backends/vulkan/glsl_shader.vert.h>
#include <backends/vulkan/glsl_shader.frag.h>
#include <toast_gpu/api.hpp>

#ifndef IM_MAX
#define IM_MAX(A, B)    (((A) >= (B)) ? (A) : (B))
#endif

// Forward Declarations
struct ImGui_ImplToaster_FrameRenderBuffers;
struct ImGui_ImplToaster_ToasterRenderBuffers;
bool ImGui_ImplToaster_CreateDeviceObjects();
void ImGui_ImplToaster_DestroyDeviceObjects();
void ImGui_ImplToaster_DestroyFrameRenderBuffers(ImGui_ImplToaster_FrameRenderBuffers *buffers);
void ImGui_ImplToaster_DestroyWindowRenderBuffers(ImGui_ImplToaster_ToasterRenderBuffers *buffers);

// Reusable buffers used for rendering 1 current in-flight frame, for ImGui_ImplToaster_RenderDrawData()
// [Please zero-clear before use!]
struct ImGui_ImplToaster_FrameRenderBuffers
{
	toaster::gpu::BufferHandle vertexBuffer{nullptr};
	toaster::gpu::BufferHandle indexBuffer{nullptr};
	uint64                     indexBufferSize{0u};
	uint64                     vertexBufferSize{0u};
};

struct ImGui_ImplToaster_ToasterRenderBuffers
{
	uint32                                         Index;
	uint32                                         Count;
	ImVector<ImGui_ImplToaster_FrameRenderBuffers> FrameRenderBuffers;
};

struct ImGui_ImplToaster_Texture
{
	toaster::gpu::TextureHandle texture{nullptr};
	uint32                      heapSlot{UINT32_MAX};
};

// For multi-viewport support:
// Helper structure we store in the void* RendererUserData field of each ImGuiViewport to easily retrieve our backend data.
struct ImGui_ImplToaster_ViewportData
{
	ImGui_ImplToaster_ToasterRenderBuffers RenderBuffers{}; // Used by all viewports
	bool                                   WindowOwned{false};
	bool                                   SwapChainNeedRebuild{false}; // Flag when viewport swapchain resized in the middle of processing a frame
	bool                                   SwapChainSuboptimal{false};  // Flag when VK_SUBOPTIMAL_KHR was returned.
};

struct ImGui_ImplToaster_Data
{
	ImGui_ImplToaster_InitInfo ToasterInitInfo;

	toaster::gpu::GPUContext *   gpuCtx{nullptr};
	toaster::gpu::UploadContext *uploadCtx{nullptr};

	toaster::gpu::ShaderHandle vertexShader{nullptr};
	toaster::gpu::ShaderHandle fragmentShader{nullptr};

	toaster::gpu::ResourceDescriptorHeapHandle resourceHeap{nullptr};
	toaster::gpu::SamplerDescriptorHeapHandle  samplerHeap{nullptr};

	uint32 TexSamplerLinearHeapSlot;

	uint64 bufferMemoryAlignment{256u};
};

static ImGui_ImplToaster_Data *ImGui_ImplToaster_GetBackendData()
{
	return ImGui::GetCurrentContext() ? (ImGui_ImplToaster_Data *) ImGui::GetIO().BackendRendererUserData : nullptr;
}

static void CreateOrResizeBuffer(toaster::gpu::BufferHandle &p_out_buffer, uint64 &p_out_size, uint64 p_new_size, toaster::gpu::EBufferUsageFlags p_usage_flags)
{
	ImGui_ImplToaster_Data *    bd = ImGui_ImplToaster_GetBackendData();
	ImGui_ImplToaster_InitInfo *v  = &bd->ToasterInitInfo;

	if (p_out_buffer)
		toaster::gpu::buffer_destroy(bd->gpuCtx, p_out_buffer);

	uint64                   buffer_size_aligned = TST_ALIGN(IM_MAX(v->minAllocationSize, p_new_size), bd->bufferMemoryAlignment);
	toaster::gpu::BufferDesc buffer_desc{};
	buffer_desc.size       = buffer_size_aligned;
	buffer_desc.usage      = p_usage_flags;
	buffer_desc.memoryType = toaster::gpu::EMemoryType::eHostVisibleCoherent;
	p_out_buffer           = toaster::gpu::buffer_create(bd->gpuCtx, buffer_desc);

	p_out_size = buffer_size_aligned;
}

struct PushData
{
	uintptr vertexBufferAddress;

	toaster::math::Float2 scale;
	toaster::math::Float2 translate;

	struct TexData
	{
		uint32 texHeapSlot;
		uint32 samplerHeapSlot;
	};

	TexData texData;
};

static void ImGui_ImplToaster_SetupRenderState(ImDrawData *draw_data, toaster::gpu::CommandListHandle p_command_list, ImGui_ImplToaster_FrameRenderBuffers *rb,
											   int         fb_width, int                              fb_height)
{
	ImGui_ImplToaster_Data *bd = ImGui_ImplToaster_GetBackendData();

	toaster::gpu::cmd_bind_shaders(bd->gpuCtx, p_command_list, {bd->vertexShader, bd->fragmentShader});

	toaster::gpu::cmd_set_primitive_topology(bd->gpuCtx, p_command_list, toaster::gpu::EPrimitiveTopology::eTriangleList);
	toaster::gpu::cmd_set_primitive_restart(bd->gpuCtx, p_command_list, false);

	toaster::gpu::cmd_set_rasterizer_discard_enable(bd->gpuCtx, p_command_list, false);
	toaster::gpu::cmd_set_polygon_mode(bd->gpuCtx, p_command_list, toaster::gpu::EPolygonMode::eFill);
	toaster::gpu::cmd_set_cull_mode(bd->gpuCtx, p_command_list, toaster::gpu::ECullMode::eNone);
	toaster::gpu::cmd_set_front_face(bd->gpuCtx, p_command_list, toaster::gpu::EFrontFace::eCCW);
	toaster::gpu::cmd_set_depth_bias(bd->gpuCtx, p_command_list, false);
	toaster::gpu::cmd_set_line_width(bd->gpuCtx, p_command_list, 1.0f);

	toaster::gpu::cmd_set_rasterization_samples(bd->gpuCtx, p_command_list, toaster::gpu::ESampleCount::e1);

	toaster::gpu::cmd_set_depth_state(bd->gpuCtx, p_command_list, false);
	toaster::gpu::cmd_set_stencil_state(bd->gpuCtx, p_command_list, false);

	toaster::gpu::cmd_set_colour_write_enable(bd->gpuCtx, p_command_list, {true});
	toaster::gpu::cmd_set_colour_write_mask(bd->gpuCtx, p_command_list, {toaster::gpu::EColourComponentFlagBits::eAll});
	toaster::gpu::cmd_set_colour_blend_enable(bd->gpuCtx, p_command_list, {true});

	toaster::gpu::ColourBlendEquation blend_equation{};
	blend_equation.srcColorBlendFactor = toaster::gpu::EBlendFactor::eSrcAlpha;
	blend_equation.dstColorBlendFactor = toaster::gpu::EBlendFactor::eOneMinusSrcAlpha;
	blend_equation.colorBlendOp        = toaster::gpu::EBlendOp::eAdd;
	blend_equation.srcAlphaBlendFactor = toaster::gpu::EBlendFactor::eOne;
	blend_equation.dstAlphaBlendFactor = toaster::gpu::EBlendFactor::eOneMinusSrcAlpha;
	blend_equation.alphaBlendOp        = toaster::gpu::EBlendOp::eAdd;
	toaster::gpu::cmd_set_colour_blend_equation(bd->gpuCtx, p_command_list, blend_equation);

	if (draw_data->TotalVtxCount > 0)
	{
		toaster::gpu::cmd_bind_index_buffer(bd->gpuCtx, p_command_list, rb->indexBuffer, toaster::gpu::EIndexType::eUint16);
	}

	toaster::gpu::cmd_set_viewport(bd->gpuCtx, p_command_list, toaster::math::Viewport{{static_cast<float32>(fb_width), static_cast<float32>(fb_height)}});

	PushData push_data{};

	push_data.vertexBufferAddress = toaster::gpu::buffer_get_address(bd->gpuCtx, rb->vertexBuffer);

	push_data.scale.x     = 2.0f / draw_data->DisplaySize.x;
	push_data.scale.y     = 2.0f / draw_data->DisplaySize.y;
	push_data.translate.x = -1.0f - draw_data->DisplayPos.x * push_data.scale.x;
	push_data.translate.y = -1.0f - draw_data->DisplayPos.y * push_data.scale.y;

	toaster::gpu::cmd_push_data(bd->gpuCtx, p_command_list, push_data);
}

// Render function
void ImGui_ImplToaster_RenderDrawData(ImDrawData *draw_data, toaster::gpu::CommandListHandle p_command_list)
{
	// Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
	int fb_width  = (int) (draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
	int fb_height = (int) (draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
	if (fb_width <= 0 || fb_height <= 0)
		return;

	// Catch up with texture updates. Most of the times, the list will have 1 element with an OK status, aka nothing to do.
	// (This almost always points to ImGui::GetPlatformIO().Textures[] but is part of ImDrawData to allow overriding or disabling texture updates).
	if (draw_data->Textures != nullptr)
		for (ImTextureData *tex: *draw_data->Textures)
			if (tex->Status != ImTextureStatus_OK)
				ImGui_ImplToaster_UpdateTexture(tex);

	ImGui_ImplToaster_Data *    bd = ImGui_ImplToaster_GetBackendData();
	ImGui_ImplToaster_InitInfo *v  = &bd->ToasterInitInfo;

	// Allocate array to store enough vertex/index buffers. Each unique viewport gets its own storage.
	auto *viewport_renderer_data = static_cast<ImGui_ImplToaster_ViewportData *>(draw_data->OwnerViewport->RendererUserData);
	IM_ASSERT(viewport_renderer_data != nullptr);
	ImGui_ImplToaster_ToasterRenderBuffers *wrb = &viewport_renderer_data->RenderBuffers;
	if (wrb->FrameRenderBuffers.Size == 0)
	{
		wrb->Index = 0;
		wrb->Count = v->maxFramesInFlight;
		wrb->FrameRenderBuffers.resize(wrb->Count);
		memset((void *) wrb->FrameRenderBuffers.Data, 0, wrb->FrameRenderBuffers.size_in_bytes());
	}
	IM_ASSERT(wrb->Count == v->maxFramesInFlight);
	wrb->Index                               = (wrb->Index + 1) % wrb->Count;
	ImGui_ImplToaster_FrameRenderBuffers *rb = &wrb->FrameRenderBuffers[wrb->Index];

	if (draw_data->TotalVtxCount > 0)
	{
		// Create or resize the vertex/index buffers
		uint64 vertex_size = TST_ALIGN(draw_data->TotalVtxCount * sizeof(ImDrawVert), bd->bufferMemoryAlignment);
		uint64 index_size  = TST_ALIGN(draw_data->TotalIdxCount * sizeof(ImDrawIdx), bd->bufferMemoryAlignment);

		if (!rb->vertexBuffer || rb->vertexBufferSize < vertex_size)
			CreateOrResizeBuffer(rb->vertexBuffer, rb->vertexBufferSize, vertex_size, toaster::gpu::EBufferUsageFlagBits::eStorageBuffer);
		if (!rb->indexBuffer || rb->indexBufferSize < index_size)
			CreateOrResizeBuffer(rb->indexBuffer, rb->indexBufferSize, index_size, toaster::gpu::EBufferUsageFlagBits::eIndexBuffer);

		// Upload vertex/index data into a single contiguous GPU buffer
		ImDrawVert *vtx_dst{static_cast<ImDrawVert *>(toaster::gpu::buffer_get_mapped_data(bd->gpuCtx, rb->vertexBuffer))};
		ImDrawIdx * idx_dst{static_cast<ImDrawIdx *>(toaster::gpu::buffer_get_mapped_data(bd->gpuCtx, rb->indexBuffer))};

		for (const ImDrawList *draw_list: draw_data->CmdLists)
		{
			std::memcpy(vtx_dst, draw_list->VtxBuffer.Data, draw_list->VtxBuffer.Size * sizeof(ImDrawVert));
			std::memcpy(idx_dst, draw_list->IdxBuffer.Data, draw_list->IdxBuffer.Size * sizeof(ImDrawIdx));
			vtx_dst += draw_list->VtxBuffer.Size;
			idx_dst += draw_list->IdxBuffer.Size;
		}
	}

	// Setup desired Vulkan state
	ImGui_ImplToaster_SetupRenderState(draw_data, p_command_list, rb, fb_width, fb_height);

	// Setup render state structure (for callbacks and custom texture bindings)
	ImGuiPlatformIO &             platform_io = ImGui::GetPlatformIO();
	ImGui_ImplToaster_RenderState render_state;
	render_state.commandList         = p_command_list;
	platform_io.Renderer_RenderState = &render_state;

	// Will project scissor/clipping rectangles into framebuffer space
	ImVec2 clip_off   = draw_data->DisplayPos;       // (0,0) unless using multi-viewports
	ImVec2 clip_scale = draw_data->FramebufferScale; // (1,1) unless using retina display which are often (2,2)

	// Render command lists
	// (Because we merged all buffers into a single one, we maintain our own offset into them)
	uint32 last_tex_heap_slot{UINT32_MAX};
	int    global_vtx_offset = 0;
	int    global_idx_offset = 0;
	for (const ImDrawList *draw_list: draw_data->CmdLists)
	{
		for (int cmd_i = 0; cmd_i < draw_list->CmdBuffer.Size; cmd_i++)
		{
			const ImDrawCmd *pcmd = &draw_list->CmdBuffer[cmd_i];
			if (pcmd->UserCallback != nullptr)
			{
				// User callback, registered via ImDrawList::AddCallback()
				// (ImDrawCallback_ResetRenderState is a special callback value used by the user to request the renderer to reset render state.)
				if (pcmd->UserCallback == ImDrawCallback_ResetRenderState)
					ImGui_ImplToaster_SetupRenderState(draw_data, p_command_list, rb, fb_width, fb_height);
				else
					pcmd->UserCallback(draw_list, pcmd);
				last_tex_heap_slot = UINT32_MAX;
			}
			else
			{
				// Project scissor/clipping rectangles into framebuffer space
				ImVec2 clip_min((pcmd->ClipRect.x - clip_off.x) * clip_scale.x, (pcmd->ClipRect.y - clip_off.y) * clip_scale.y);
				ImVec2 clip_max((pcmd->ClipRect.z - clip_off.x) * clip_scale.x, (pcmd->ClipRect.w - clip_off.y) * clip_scale.y);

				// Clamp to viewport as vkCmdSetScissor() won't accept values that are off bounds
				if (clip_min.x < 0.0f)
					clip_min.x = 0.0f;
				if (clip_min.y < 0.0f)
					clip_min.y = 0.0f;
				if (clip_max.x > static_cast<float32>(fb_width))
					clip_max.x = static_cast<float32>(fb_width);
				if (clip_max.y > static_cast<float32>(fb_height))
					clip_max.y = static_cast<float32>(fb_height);
				if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
					continue;

				// Apply scissor/clipping rectangle
				toaster::math::Rect scissor{};
				scissor.offset.x = static_cast<int32>(clip_min.x);
				scissor.offset.y = static_cast<int32>(clip_min.y);
				scissor.size.x   = static_cast<uint32>(clip_max.x - clip_min.x);
				scissor.size.y   = static_cast<uint32>(clip_max.y - clip_min.y);
				toaster::gpu::cmd_set_scissor(bd->gpuCtx, p_command_list, scissor);

				// Bind DescriptorSet with font or user texture
				auto tex_heap_slot = static_cast<uint32>(pcmd->GetTexID());
				if (tex_heap_slot != last_tex_heap_slot)
				{
					PushData::TexData tex_data{};
					tex_data.texHeapSlot     = tex_heap_slot;
					tex_data.samplerHeapSlot = bd->TexSamplerLinearHeapSlot;
					IM_ASSERT(tex_data.texHeapSlot != UINT32_MAX && tex_data.samplerHeapSlot != UINT32_MAX);
					toaster::gpu::cmd_push_data<PushData::TexData>(bd->gpuCtx, p_command_list, tex_data, offsetof(PushData, texData));
				}
				last_tex_heap_slot = tex_heap_slot;

				// Draw
				toaster::gpu::cmd_draw_indexed(bd->gpuCtx, p_command_list, pcmd->ElemCount, 1, pcmd->IdxOffset + global_idx_offset,
											   static_cast<int32>(pcmd->VtxOffset) + global_vtx_offset, 0);
			}
		}
		global_idx_offset += draw_list->IdxBuffer.Size;
		global_vtx_offset += draw_list->VtxBuffer.Size;
	}
	platform_io.Renderer_RenderState = nullptr;

	toaster::gpu::cmd_set_scissor(bd->gpuCtx, p_command_list, toaster::math::Rect{{static_cast<uint32>(fb_width), static_cast<uint32>(fb_height)}});
}

static void ImGui_ImplToaster_DestroyTexture(ImTextureData *tex)
{
	if (auto *backend_tex = static_cast<ImGui_ImplToaster_Texture *>(tex->BackendUserData))
	{
		IM_ASSERT(backend_tex->heapSlot == static_cast<uint32>(tex->TexID));
		ImGui_ImplToaster_Data *    bd = ImGui_ImplToaster_GetBackendData();
		ImGui_ImplToaster_InitInfo *v  = &bd->ToasterInitInfo;
		ImGui_ImplToaster_RemoveTexture(backend_tex->heapSlot);
		toaster::gpu::texture_destroy(bd->gpuCtx, backend_tex->texture);
		IM_DELETE(backend_tex);

		// Clear identifiers and mark as destroyed (in order to allow e.g. calling InvalidateDeviceObjects while running)
		tex->SetTexID(ImTextureID_Invalid);
		tex->BackendUserData = nullptr;
	}
	tex->SetStatus(ImTextureStatus_Destroyed);
}

void ImGui_ImplToaster_UpdateTexture(ImTextureData *tex)
{
	if (tex->Status == ImTextureStatus_OK)
		return;
	ImGui_ImplToaster_Data *    bd = ImGui_ImplToaster_GetBackendData();
	ImGui_ImplToaster_InitInfo *v  = &bd->ToasterInitInfo;

	if (tex->Status == ImTextureStatus_WantCreate)
	{
		IM_ASSERT(tex->TexID == ImTextureID_Invalid && tex->BackendUserData == nullptr);
		IM_ASSERT(tex->Format == ImTextureFormat_RGBA32);
		auto backend_tex = IM_NEW(ImGui_ImplToaster_Texture)();

		toaster::gpu::TextureDesc texture_desc{};
		texture_desc.extent      = {static_cast<uint32>(tex->Width), static_cast<uint32>(tex->Height), 1u};
		texture_desc.format      = toaster::gpu::EFormat::eR8G8B8A8Unorm;
		texture_desc.type        = toaster::gpu::ETextureType::e2D;
		texture_desc.layerCount  = 1u;
		texture_desc.mipCount    = 1u;
		texture_desc.sampleCount = toaster::gpu::ESampleCount::e1;
		texture_desc.usage       = toaster::gpu::ETextureUsageFlagBits::eSampled | toaster::gpu::ETextureUsageFlagBits::eTransferDst;

		backend_tex->texture  = toaster::gpu::texture_create(bd->gpuCtx, texture_desc);
		backend_tex->heapSlot = ImGui_ImplToaster_AddTexture(backend_tex->texture);

		// Store identifiers
		tex->SetTexID(static_cast<ImTextureID>(backend_tex->heapSlot));
		tex->BackendUserData = backend_tex;
	}

	if (tex->Status == ImTextureStatus_WantCreate || tex->Status == ImTextureStatus_WantUpdates)
	{
		auto *backend_tex = static_cast<ImGui_ImplToaster_Texture *>(tex->BackendUserData);
		IM_ASSERT(backend_tex);

		// Update full texture or selected blocks. We only ever write to textures regions which have never been used before!
		// This backend choose to use tex->UpdateRect but you can use tex->Updates[] to upload individual regions.
		// We could use the smaller rect on _WantCreate but using the full rect allows us to clear the texture.
		const int32 upload_x = (tex->Status == ImTextureStatus_WantCreate) ? 0 : tex->UpdateRect.x;
		const int32 upload_y = (tex->Status == ImTextureStatus_WantCreate) ? 0 : tex->UpdateRect.y;
		const int32 upload_w = (tex->Status == ImTextureStatus_WantCreate) ? tex->Width : tex->UpdateRect.w;
		const int32 upload_h = (tex->Status == ImTextureStatus_WantCreate) ? tex->Height : tex->UpdateRect.h;

		uint64 upload_pitch{(uint64) upload_w * tex->BytesPerPixel};

		toaster::gpu::TextureUploadDesc upload_desc{};
		upload_desc.size       = upload_pitch * upload_h;
		upload_desc.extent     = {static_cast<uint32>(upload_w), static_cast<uint32>(upload_h), 1u};
		upload_desc.offset     = {upload_x, upload_y, 0};
		upload_desc.layerCount = 1u;
		upload_desc.baseLayer  = 0u;
		upload_desc.dstTexture = backend_tex->texture;

		std::vector<uint8> upload_data(upload_desc.size);
		for (int y = 0; y < upload_h; y++)
			memcpy(upload_data.data() + upload_pitch * y, tex->GetPixelsAt(upload_x, upload_y + y), upload_pitch);

		upload_desc.data = upload_data.data();

		const auto state_tracker{toaster::gpu::state_tracker_register(bd->uploadCtx, 1u)};
		toaster::gpu::upload_data_to_texture(bd->uploadCtx, upload_desc, state_tracker);
		toaster::gpu::state_tracker_wait_for(bd->uploadCtx, state_tracker);

		tex->SetStatus(ImTextureStatus_OK);
	}

	if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames >= (int) bd->ToasterInitInfo.maxFramesInFlight)
		ImGui_ImplToaster_DestroyTexture(tex);
}

static void ImGui_ImplToaster_CreateShaderModules()
{
	ImGui_ImplToaster_Data *bd = ImGui_ImplToaster_GetBackendData();
	if (!bd->vertexShader)
	{
		toaster::gpu::ShaderDesc vertex_shader_desc{};
		vertex_shader_desc.entryPointName = "main";
		vertex_shader_desc.code           = g_vertex_shader_bytecode;
		vertex_shader_desc.codeSizeWords  = sizeof(g_vertex_shader_bytecode) / sizeof(uint32);
		vertex_shader_desc.stage          = toaster::gpu::EShaderStageFlagBits::eVertex;
		vertex_shader_desc.nextStage      = toaster::gpu::EShaderStageFlagBits::ePixel;

		bd->vertexShader = toaster::gpu::shader_create(bd->gpuCtx, vertex_shader_desc);
	}
	if (!bd->fragmentShader)
	{
		toaster::gpu::ShaderDesc fragment_shader_desc{};
		fragment_shader_desc.entryPointName = "main";
		fragment_shader_desc.code           = g_fragment_shader_bytecode;
		fragment_shader_desc.codeSizeWords  = sizeof(g_fragment_shader_bytecode) / sizeof(uint32);
		fragment_shader_desc.stage          = toaster::gpu::EShaderStageFlagBits::ePixel;
		fragment_shader_desc.nextStage      = toaster::gpu::EShaderStageFlagBits::eNone;

		bd->fragmentShader = toaster::gpu::shader_create(bd->gpuCtx, fragment_shader_desc);
	}
}

bool ImGui_ImplToaster_CreateDeviceObjects()
{
	ImGui_ImplToaster_Data *    bd = ImGui_ImplToaster_GetBackendData();
	ImGui_ImplToaster_InitInfo *v  = &bd->ToasterInitInfo;

	if (bd->TexSamplerLinearHeapSlot == UINT32_MAX)
	{
		toaster::gpu::SamplerDesc sampler_desc{};
		sampler_desc.minFilter     = toaster::gpu::EFilter::eLinear;
		sampler_desc.magFilter     = toaster::gpu::EFilter::eLinear;
		sampler_desc.mipmapMode    = toaster::gpu::ESamplerMipmapMode::eLinear;
		sampler_desc.addressModeU  = toaster::gpu::ESamplerAddressMode::eClampToEdge;
		sampler_desc.addressModeV  = toaster::gpu::ESamplerAddressMode::eClampToEdge;
		sampler_desc.addressModeW  = toaster::gpu::ESamplerAddressMode::eClampToEdge;
		sampler_desc.minLod        = -1000.0f;
		sampler_desc.maxLod        = 1000.0f;
		sampler_desc.maxAnisotropy = 1.0f;

		bd->TexSamplerLinearHeapSlot = toaster::gpu::sampler_descriptor_heap_alloc_sampler_slot(bd->gpuCtx, bd->samplerHeap);
		toaster::gpu::sampler_descriptor_heap_write_sampler(bd->gpuCtx, bd->samplerHeap, bd->TexSamplerLinearHeapSlot, sampler_desc);
	}

	ImGui_ImplToaster_CreateShaderModules();

	return true;
}

void ImGui_ImplToaster_DestroyDeviceObjects()
{
	ImGui_ImplToaster_Data *    bd = ImGui_ImplToaster_GetBackendData();
	ImGui_ImplToaster_InitInfo *v  = &bd->ToasterInitInfo;
	// ImGui_ImplToasterH_DestroyAllViewportsRenderBuffers(v->Device, v->Allocator);

	// Destroy all textures
	for (ImTextureData *tex: ImGui::GetPlatformIO().Textures)
		if (tex->RefCount == 1)
			ImGui_ImplToaster_DestroyTexture(tex);

	if (bd->TexSamplerLinearHeapSlot != UINT32_MAX)
		toaster::gpu::sampler_descriptor_heap_free_sampler_slot(bd->gpuCtx, bd->samplerHeap, bd->TexSamplerLinearHeapSlot);

	if (bd->vertexShader)
		toaster::gpu::shader_destroy(bd->gpuCtx, bd->vertexShader);
	if (bd->fragmentShader)
		toaster::gpu::shader_destroy(bd->gpuCtx, bd->fragmentShader);
}

bool ImGui_ImplToaster_Init(ImGui_ImplToaster_InitInfo &info)
{
	ImGuiIO &io = ImGui::GetIO();
	IMGUI_CHECKVERSION();
	IM_ASSERT(io.BackendRendererUserData == nullptr && "Already initialized a renderer backend!");

	// Setup backend capabilities flags
	auto *bd                   = IM_NEW(ImGui_ImplToaster_Data)();
	io.BackendRendererUserData = static_cast<void *>(bd);
	io.BackendRendererName     = "imgui_impl_toaster";
	io.BackendFlags            |= ImGuiBackendFlags_RendererHasVtxOffset; // We can honor the ImDrawCmd::VtxOffset field, allowing for large meshes.
	io.BackendFlags            |= ImGuiBackendFlags_RendererHasTextures;  // We can honor ImGuiPlatformIO::Textures[] requests during render.
	io.BackendFlags            |= ImGuiBackendFlags_RendererHasViewports; // We can create multi-viewports on the Renderer side (optional)

	bd->ToasterInitInfo = info;

	bd->gpuCtx    = info.gpuCtx;
	bd->uploadCtx = info.uploadCtx;

	bd->resourceHeap = info.resourceHeap;
	bd->samplerHeap  = info.samplerHeap;

	if (!ImGui_ImplToaster_CreateDeviceObjects())
		IM_ASSERT(0 && "ImGui_ImplToaster_CreateDeviceObjects() failed!"); // <- Can't be hit yet.

	// Our render function expect RendererUserData to be storing the window render buffer we need (for the main viewport we won't use ->Window)
	ImGuiViewport *main_viewport    = ImGui::GetMainViewport();
	main_viewport->RendererUserData = IM_NEW(ImGui_ImplToaster_ViewportData)();

	return true;
}

void ImGui_ImplToaster_Shutdown()
{
	ImGui_ImplToaster_Data *bd = ImGui_ImplToaster_GetBackendData();
	IM_ASSERT(bd != nullptr && "No renderer backend to shutdown, or already shutdown?");
	ImGuiIO &        io          = ImGui::GetIO();
	ImGuiPlatformIO &platform_io = ImGui::GetPlatformIO();

	// First destroy objects in all viewports
	ImGui_ImplToaster_DestroyDeviceObjects();

	// Manually delete main viewport render data in-case we haven't initialized for viewports
	ImGuiViewport *main_viewport = ImGui::GetMainViewport();
	if (ImGui_ImplToaster_ViewportData *vd = (ImGui_ImplToaster_ViewportData *) main_viewport->RendererUserData)
		IM_DELETE(vd);
	main_viewport->RendererUserData = nullptr;

	io.BackendRendererName     = nullptr;
	io.BackendRendererUserData = nullptr;
	io.BackendFlags            &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasViewports);
	platform_io.ClearRendererHandlers();
	IM_DELETE(bd);
}

void ImGui_ImplToaster_NewFrame()
{
	ImGui_ImplToaster_Data *bd = ImGui_ImplToaster_GetBackendData();
	IM_ASSERT(bd != nullptr && "Context or backend not initialized! Did you call ImGui_ImplToaster_Init()?");
	IM_UNUSED(bd);
}

uint32 ImGui_ImplToaster_AddTexture(toaster::gpu::TextureHandle p_texture)
{
	const ImGui_ImplToaster_Data *bd = ImGui_ImplToaster_GetBackendData();

	const uint32 heap_slot{toaster::gpu::resource_descriptor_heap_alloc_texture_slot(bd->gpuCtx, bd->resourceHeap)};
	toaster::gpu::resource_descriptor_heap_write_texture(bd->gpuCtx, bd->resourceHeap, heap_slot, p_texture, false);

	return heap_slot;
}

void ImGui_ImplToaster_RemoveTexture(uint32 p_texture_slot)
{
	ImGui_ImplToaster_Data *bd = ImGui_ImplToaster_GetBackendData();
	toaster::gpu::resource_descriptor_heap_free_texture_slot(bd->gpuCtx, bd->resourceHeap, p_texture_slot);
}
#endif // #ifndef IMGUI_DISABLE
