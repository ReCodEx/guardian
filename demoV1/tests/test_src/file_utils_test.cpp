#include "utils.hpp"

int main(int argc, char** argv)
{
    size_t row = std::stoi(argv[1]);
    size_t col = std::stoi(argv[2]);
    std::ifstream cpu_stat("/sys/fs/cgroup/cpu.stat");
    std::cout << file_utils::read_row_col(cpu_stat, row, col) << std::endl;
}