#include <filesystem>
#include <iostream>
#include <toast_os/console.hpp>
#include <toast_os/entry_points.hpp>

#include <toast_gpu/api.hpp>
#include <toast_gpu/upload.hpp>

#include <toast_gpu/allocation.hpp>
#include <toast_gpu/frame.hpp>
#include <toast_kernel/window.hpp>
#include <toast_kernel/events/event.hpp>

using namespace toaster;

#include <backends/imgui_impl_toaster.h>
#include <backends/imgui_impl_glfw.h>

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#pragma comment(lib, "winmm.lib")

TST_OS_WINMAIN()
{
	#ifndef NDEBUG
	os::output_console_create("Toaster Test Debug Output");
	#endif

	rpmalloc_interface_t malloc_interface{};
	malloc_interface.error_callback = +[](CString p_message) -> void
	{
		printf("rpmalloc Error: %s\n", p_message);
	};
	rpmalloc_initialize(&malloc_interface);

	#if 1

	struct Application;

	struct AppLayer
	{
		using InitFn     = void(*)(Application &, void *);
		using DestroyFn  = void(*)(Application &, void *);
		using OnRenderFn = void(*)(Application &, void *, gpu::CommandListHandle);
		using OnUpdateFn = void(*)(Application &, void *, float32);
		using OnEventFn  = void(*)(Application &, void *, knl::Event &);

		InitFn     initFn{nullptr};
		DestroyFn  destroyFn{nullptr};
		OnRenderFn onRenderFn{nullptr};
		OnUpdateFn onUpdateFn{nullptr};
		OnEventFn  onEventFn{nullptr};

		void *userData{nullptr}; // This could be a stack of heap pointer, as it is managed by the end-user
	};

	static constexpr uint32 c_max_frames_in_flight{3u};

	struct Application
	{
		gpu::GPUContext *                 gpuCtx{nullptr};
		gpu::FrameContext *               frameCtx{nullptr};
		gpu::AllocationContext *          allocCtx{nullptr};
		gpu::UploadContext *              uploadCtx{nullptr};
		gpu::ResourceDescriptorHeapHandle resourceHeap{nullptr};
		gpu::SamplerDescriptorHeapHandle  samplerHeap{nullptr};

		knl::Window window;

		Array<AppLayer, 1u> layers;

		uint32  frameIndex{0u};
		uint32  maxFramesInFlight{c_max_frames_in_flight};
		float32 deltaTime{0.0f};
	};

	Application app;

	{
		// Create the GPU contexts
		gpu::GPUContextDesc gpu_ctx_desc{};
		#ifndef NDEBUG
		gpu_ctx_desc.enableDebugInfo = false;
		#else
		gpu_ctx_desc.enableDebugInfo = false;
		#endif
		gpu_ctx_desc.usingSwapchain                  = true;
		gpu_ctx_desc.maxConcurrentSwapchainWorkloads = c_max_frames_in_flight;
		app.gpuCtx                                   = gpu::gpu_ctx_create(gpu_ctx_desc);
		app.frameCtx                                 = gpu::frame_ctx_create(app.gpuCtx, gpu::FrameContextDesc{.maxFramesInFlight = c_max_frames_in_flight});
		app.allocCtx                                 = gpu::allocation_ctx_create();
		app.uploadCtx                                = gpu::upload_ctx_create(app.gpuCtx, app.allocCtx, gpu::UploadContextDesc{.maxAllocationCommandLists = 2u});

		app.resourceHeap = gpu::resource_descriptor_heap_create(app.gpuCtx, {128u, 128u});
		app.samplerHeap  = gpu::sampler_descriptor_heap_create(app.gpuCtx, {8u});

		knl::window_init_api();

		knl::WindowDesc window_desc{};
		window_desc.title          = "Toaster Test";
		window_desc.size           = {1920u, 1080u};
		window_desc.startMaximized = false;
		knl::window_create(app.window, app.gpuCtx, window_desc);

		knl::window_set_event_callback_user_data(app.window, &app);
		knl::window_set_event_callback(app.window, +[](knl::Event &p_event, void *p_user_data) -> void
		{
			for (const auto app{static_cast<Application *>(p_user_data)}; const auto &layer: app->layers)
			{
				if (p_event.isHandled())
					continue;

				if (layer.onEventFn)
					layer.onEventFn(*app, layer.userData, p_event);
			}
		});

		struct RenderTexture
		{
			gpu::TextureHandle      texture{nullptr};
			gpu::StateTrackerHandle stateTracker{nullptr};
			uint32                  shaderReadHeapSlot;
		};

		struct TestLayer
		{
		};

		TestLayer test_layer{};

		AppLayer &app_layer{app.layers[0u]};
		app_layer.userData = &test_layer;
		app_layer.initFn   = +[](Application &p_app, void *p_layer) mutable -> void
		{
			auto layer{static_cast<TestLayer *>(p_layer)};

			(void) ImGui::CreateContext();
			ImGui_ImplToaster_InitInfo imgui_toaster_init_info{
				.gpuCtx = p_app.gpuCtx,
				.uploadCtx = p_app.uploadCtx,
				.resourceHeap = p_app.resourceHeap,
				.samplerHeap = p_app.samplerHeap,
				.maxFramesInFlight = c_max_frames_in_flight
			};
			ImGui_ImplToaster_Init(imgui_toaster_init_info);
			ImGui_ImplGlfw_InitForVulkan(p_app.window.window, true);
		};
		app_layer.destroyFn = +[](Application &p_app, void *p_layer) mutable -> void
		{
			ImGui_ImplToaster_Shutdown();
			ImGui_ImplGlfw_Shutdown();
			ImGui::DestroyContext();
		};
		app_layer.onUpdateFn = +[](Application &p_app, void *p_layer, float32 p_dt) mutable -> void
		{
		};
		app_layer.onRenderFn = +[](Application &p_app, void *p_layer, gpu::CommandListHandle p_cmd) mutable -> void
		{
			const gpu::TextureHandle swapchain_target{p_app.window.currentTexture};

			gpu::cmd_bind_resource_heap(p_app.gpuCtx, p_cmd, p_app.resourceHeap);
			gpu::cmd_bind_sampler_heap(p_app.gpuCtx, p_cmd, p_app.samplerHeap);

			ImGui_ImplToaster_NewFrame();
			ImGui_ImplGlfw_NewFrame();
			ImGui::NewFrame();

			ImGui::ShowDemoWindow();

			ImGui::Render();

			const gpu::RenderingInfo rendering_info{
				.renderArea = knl::window_get_size(p_app.window),
				.colourAttachments = {
					gpu::RenderingAttachmentInfo{
						.clearValue = gpu::ClearColourValue{1.0f, 0.0f, 1.0f, 1.0f},
						.renderTarget = swapchain_target,
						.resolveTarget = nullptr,
						.usageOp = gpu::EAttachmentUsageOP::eClearStore,
						.resolveMode = gpu::EAttachmentResolveMode::eNone
					}
				}
			};

			gpu::cmd_begin_rendering(p_app.gpuCtx, p_cmd, rendering_info);

			ImGui_ImplToaster_RenderDrawData(ImGui::GetDrawData(), p_cmd);

			gpu::cmd_end_rendering(p_app.gpuCtx, p_cmd);

			if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
			{
				ImGui::UpdatePlatformWindows();
				ImGui::RenderPlatformWindowsDefault();
			}
		};

		app.layers[0u].initFn(app, app.layers[0u].userData);

		Vector<gpu::CommandAllocatorHandle> command_allocators(app.maxFramesInFlight);
		Vector<gpu::CommandListHandle>      command_lists(app.maxFramesInFlight);
		for (uint32 i{0u}; i < app.maxFramesInFlight; ++i)
		{
			command_allocators[i] = gpu::cmd_allocator_create(app.gpuCtx, gpu::EQueueType::eGraphics);
			gpu::cmd_list_allocate_n(app.gpuCtx, command_allocators[i], std::span<gpu::CommandListHandle>{&command_lists[i], 1ull});
		}

		timeBeginPeriod(1u);

		auto last_time{std::chrono::steady_clock::now()};
		// gpu::swapchain_set_max_framerate(app.gpuCtx, app.window.swapchain, gpu::c_uncappedFramerate); // vkSetLatencySleepModeNV
		while (true)
		{
			// gpu::swapchain_wait_for_framerate(app.gpuCtx, app.window.swapchain); // vkLatencySleepNV + vkWaitSemaphores
			// gpu::swapchain_set_frame_marker(app.gpuCtx, app.window.swapchain, gpu::EFrameMarker::eInputSample);

			if (!window_process_messages(app.window)) // glfwPollEvents. returns !glfwWindowShouldClose(...);
				break;

			if (!window_check_for_resize(app.window))
				continue;

			gpu::poll_uploads(app.uploadCtx);
			gpu::begin_frame(app.frameCtx, app.frameIndex);

			auto                           current_time{std::chrono::steady_clock::now()};
			std::chrono::duration<float32> elapsed{current_time - last_time};
			app.deltaTime = elapsed.count();
			last_time     = current_time;

			gpu::CommandListHandle &cmd{command_lists[app.frameIndex]};
			gpu::cmd_allocator_reset(app.gpuCtx, command_allocators[app.frameIndex]);
			gpu::cmd_list_open(app.gpuCtx, cmd);

			if (!window_begin_frame(app.window, cmd)) // Attempts to acquire the image
			{
				gpu::cmd_allocator_reset(app.gpuCtx, command_allocators[app.frameIndex]);
				continue;
			}

			if (!window_is_minimised(app.window))
			{
				// gpu::swapchain_set_frame_marker(app.gpuCtx, app.window.swapchain, gpu::EFrameMarker::eSimulationStart);
				for (auto &layer: app.layers)
				{
					if (layer.onUpdateFn)
						layer.onUpdateFn(app, layer.userData, app.deltaTime);
				}
				// gpu::swapchain_set_frame_marker(app.gpuCtx, app.window.swapchain, gpu::EFrameMarker::eSimulationEnd);
			}
			// gpu::swapchain_set_frame_marker(app.gpuCtx, app.window.swapchain, gpu::EFrameMarker::eRendersubmitStart);
			if (!window_is_minimised(app.window))
			{
				for (auto &layer: app.layers)
				{
					if (layer.onRenderFn)
						layer.onRenderFn(app, layer.userData, cmd);
				}
			}

			// gpu::swapchain_set_frame_marker(app.gpuCtx, app.window.swapchain, gpu::EFrameMarker::eRendersubmitEnd);
			// gpu::swapchain_set_frame_marker(app.gpuCtx, app.window.swapchain, gpu::EFrameMarker::ePresentStart);
			window_submit_and_present(app.window, app.frameCtx, cmd); // also updates the input states
			// gpu::swapchain_set_frame_marker(app.gpuCtx, app.window.swapchain, gpu::EFrameMarker::ePresentEnd);

			app.frameIndex = (app.frameIndex + 1u) % app.maxFramesInFlight;
		}

		timeEndPeriod(1u);

		gpu::wait_queue_idle(app.gpuCtx, gpu::EQueueType::eGraphics); // Wait for the commands lists to finish before destroying the allocators
		for (auto &alloc: command_allocators)
			gpu::cmd_allocator_destroy(app.gpuCtx, alloc);

		gpu::wait_idle(app.gpuCtx);
		gpu::poll_uploads(app.uploadCtx);

		for (const auto &layer: app.layers)
		{
			if (layer.destroyFn)
				layer.destroyFn(app, layer.userData);
		}
		knl::window_destroy(app.window);

		gpu::upload_ctx_destroy(app.uploadCtx);
		gpu::frame_ctx_destroy(app.frameCtx);
		gpu::allocation_ctx_destroy(app.allocCtx);
		gpu::gpu_ctx_destroy(app.gpuCtx);
	}

	knl::window_shutdown_api();

	#endif

	rpmalloc_finalize();

	#ifndef NDEBUG
	os::output_console_destroy();
	#endif

	return 0;
}
