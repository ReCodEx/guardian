
#include "cli_options.hpp"
#include "cores.hpp"
#include "logs.hpp"

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    cores::root_core core(argc, argv);
    core.run();

    logs::info("Finished!");
}
