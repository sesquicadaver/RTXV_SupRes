#include "cli.hpp"

extern "C" {
#include <libavformat/avformat.h>
}

int main(int argc, char** argv) {
  avformat_network_init();
  return rtx::cli_main(argc, argv);
}
