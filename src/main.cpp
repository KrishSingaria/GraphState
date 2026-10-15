#include "platform/platform.hpp"
#include "cli/cli.hpp"

int main(int argc, char* argv[]) {
  gs::platform::init_stdio();
  return gs::cli::run(argc, argv);
}
