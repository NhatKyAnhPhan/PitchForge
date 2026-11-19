#include <flutter/dart_project.h>
#include <flutter/flutter_view_controller.h>
#include <windows.h>

#include <cstdio>
#include <cstdlib>

#include "flutter_window.h"
#include "utils.h"

namespace {

class ScopedComApartment {
 public:
  ScopedComApartment()
      : result_(::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}

  ~ScopedComApartment() {
    if (SUCCEEDED(result_)) {
      ::CoUninitialize();
    }
  }

  bool initialized() const { return SUCCEEDED(result_); }
  HRESULT result() const { return result_; }

 private:
  HRESULT result_;
};

}  // namespace

int APIENTRY wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE prev,
                      _In_ wchar_t *command_line, _In_ int show_command) {
  // Attach to console when present (e.g., 'flutter run') or create a
  // new console when running with a debugger.
  if (!::AttachConsole(ATTACH_PARENT_PROCESS) && ::IsDebuggerPresent()) {
    CreateAndAttachConsole();
  }

  // Flutter plugins may rely on COM. Fail clearly rather than continuing with
  // an uninitialized apartment; balance both S_OK and S_FALSE with
  // CoUninitialize.
  const ScopedComApartment com_apartment;
  if (!com_apartment.initialized()) {
    wchar_t message[128];
    swprintf_s(message, L"Auralis could not initialize COM (HRESULT 0x%08X).\n",
               static_cast<unsigned int>(com_apartment.result()));
    ::OutputDebugStringW(message);
    return EXIT_FAILURE;
  }

  flutter::DartProject project(L"data");

  std::vector<std::string> command_line_arguments =
      GetCommandLineArguments();

  project.set_dart_entrypoint_arguments(std::move(command_line_arguments));

  FlutterWindow window(project);
  Win32Window::Point origin(10, 10);
  Win32Window::Size size(1280, 720);
  if (!window.Create(L"auralis_studio", origin, size)) {
    ::OutputDebugStringW(L"Auralis could not create the Flutter window.\n");
    return EXIT_FAILURE;
  }
  window.SetQuitOnClose(true);

  ::MSG msg;
  int message_result;
  while ((message_result = ::GetMessage(&msg, nullptr, 0, 0)) > 0) {
    ::TranslateMessage(&msg);
    ::DispatchMessage(&msg);
  }

  if (message_result == -1) {
    ::OutputDebugStringW(
        L"Auralis encountered an error in the Windows message loop.\n");
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
