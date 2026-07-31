
#include "cli_options.hpp"
#include "container_core.hpp"
#include "logs.hpp"

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    container_core::root_core core(argc, argv);
    core.run();

    logs::info("Finished!");
}
