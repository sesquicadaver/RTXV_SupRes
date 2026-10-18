#include "cli.hpp"

#include <iostream>
#include <string>

extern "C" {
#include <libavformat/avformat.h>
}

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdio>
#endif

#if defined(RTX_WITH_GUI)
#include "gui/app.hpp"
#endif

namespace {

bool cli_command(const std::string& argument) {
  return argument == "capabilities" || argument == "doctor" || argument == "install-check" || argument == "run" ||
         argument == "help" || argument == "--help" || argument == "-h";
}

// Команда з уже відкритого PowerShell пише в його вікно. Нову консоль це не створює.
void bind_parent_console() {
#if defined(_WIN32)
  if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
    return;
  }
  FILE* stream = nullptr;
  freopen_s(&stream, "CONOUT$", "w", stdout);
  freopen_s(&stream, "CONOUT$", "w", stderr);
  std::cout.clear();
  std::cerr.clear();
#else
  (void)0;
#endif
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1 && cli_command(argv[1])) {
    bind_parent_console();
    avformat_network_init();
    return rtx::cli_main(argc, argv);
  }
#if defined(RTX_WITH_GUI)
  return run_gui(argc, argv);
#else
  avformat_network_init();
  return rtx::cli_main(argc, argv);
#endif
}
