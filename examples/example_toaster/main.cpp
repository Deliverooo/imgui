#include <iostream>
#include <toast_os/console.hpp>
#include <toast_os/entry_points.hpp>

#include <toast_kernel/application.hpp>
#include <toast_render/transform_system.hpp>

#include <backends/imgui_impl_toaster.h>
#include <backends/imgui_impl_glfw.h>

#include <toast_asset/texture_importer.hpp>

#include <toast_kernel/events/window_event.hpp>

using namespace toaster;

class MainLayer : public IAppLayer
{
public:
	virtual auto onInit() -> void override
	{
		m_textureManager = makeUnique<render::TextureManager>(m_renderCtx);

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO &io = ImGui::GetIO();
		(void) io;
		ImGui::StyleColorsDark();

		ImGui_ImplGlfw_InitForVulkan(m_app->getWindow().getGLFWWindow(), true);

		ImGui_ImplToaster_InitInfo init_info{};
		init_info.resourceHeap = m_renderCtx->getResourceHeap();
		init_info.samplerHeap  = m_renderCtx->getSamplerHeap();
		ImGui_ImplToaster_Init(init_info);
	}

	auto onDestroy() -> void override
	{
		ImGui_ImplToaster_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();

		m_textureManager.reset();
	}

	auto onUpdate(float32 p_dt) -> void override
	{
	}

	auto onRender(gpu::CommandListHandle p_cmd) -> void override
	{
		m_textureManager->pollTextureUploads(p_cmd);

		const uint32             frame_index{m_app->getFrameIndex()};
		const gpu::TextureHandle render_tex{m_app->getWindow().getCurrentTexture()};

		gpu::bindResourceHeap(p_cmd, m_renderCtx->getResourceHeap());
		gpu::bindSamplerHeap(p_cmd, m_renderCtx->getSamplerHeap());

		ImGui_ImplToaster_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		ImGui::ShowDemoWindow();

		ImGui::Render();

		gpu::RenderingInfo rendering_info{};
		rendering_info.renderArea = tsm::Rect{m_app->getWindow().getSize()};

		rendering_info.colourAttachments = {
			gpu::RenderingAttachmentInfo{
				gpu::ClearColourValue{1.0f, 0.0f, 1.0f, 0.0f},
				render_tex,
				nullptr,
				gpu::EAttachmentUsageOP::eClearStore,
				gpu::EAttachmentResolveMode::eNone
			}
		};

		gpu::beginRendering(p_cmd, rendering_info);

		ImDrawData *draw_data{ImGui::GetDrawData()};
		ImGui_ImplToaster_RenderDrawData(draw_data, p_cmd);

		gpu::endRendering(p_cmd);
	}

	auto onEvent(Event &p_event) -> void override
	{
		EventDispatcher ed{p_event};
		ed.dispatch<WindowFileDropEvent>([this](WindowFileDropEvent &p_e) -> bool
		{
			const String &path{p_e.getFilepaths()[0u]};

			if (m_texture)
				m_textureManager->destroyTexture(m_texture);

			m_texture = m_textureManager->registerTexture();
			asset::asyncLoadTextureFromFile(m_texture, *m_textureManager, path, m_assetLoader);

			return true;
		});
	}

private:
	UniquePtr<render::TextureManager> m_textureManager{nullptr};
	tf::Executor                      m_assetLoader{};

	render::TextureHandle m_texture{nullptr};
};

TST_WINMAIN()
{
	os::createOutputConsole();

	{
		Application app{};
		app.addLayer<MainLayer>();
		app.run();
	}

	os::destroyOutputConsole();

	return 0;
}
