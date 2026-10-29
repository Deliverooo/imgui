#include <iostream>
#include <toast_os/console.hpp>
#include <toast_os/entry_points.hpp>

#include <toast_kernel/application.hpp>

#include <toast_gpu/api.hpp>
#include <toast_gpu/upload.hpp>

using namespace toaster;

TST_OS_WINMAIN()
{
	os::output_console_create("ImGui Toaster Test Debug Output");

	{
		//TODO: Actually provide an example...
	}

	os::output_console_destroy();

	return 0;
}
