#include "cli.hpp"

#include <string>

extern "C" {
#include <libavformat/avformat.h>
}

#if defined(RTX_WITH_GUI)
#include "gui/app.hpp"
#endif

namespace {

bool cli_command(const std::string& argument) {
  return argument == "capabilities" || argument == "doctor" || argument == "install-check" || argument == "run" ||
         argument == "help" || argument == "--help" || argument == "-h";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1 && cli_command(argv[1])) {
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
