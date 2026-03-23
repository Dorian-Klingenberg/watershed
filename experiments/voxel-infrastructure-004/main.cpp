#include "dashboard.h"

#include <exception>
#include <iostream>
#include <string>

using namespace voxel_infrastructure_004;

namespace
{
DashboardOptions parse_options(int argc, char **argv)
{
    DashboardOptions options;

    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];

        if (argument == "--ticks" && index + 1 < argc)
        {
            options.ticks = std::stoi(argv[++index]);
        }
        else if (argument == "--sleep-ms" && index + 1 < argc)
        {
            options.sleep_ms = std::stoi(argv[++index]);
        }
        else if (argument == "--no-clear")
        {
            options.clear_screen = false;
        }
        else if (argument == "--non-interactive")
        {
            options.interactive = false;
        }
        else if (argument == "--help")
        {
            std::cout << "Usage: voxel_infrastructure_004_experiment [--ticks N] [--sleep-ms N] [--no-clear] [--non-interactive]\n";
            std::exit(0);
        }
    }

    return options;
}
} // namespace

int main(int argc, char **argv)
{
    try
    {
        return run_dashboard(parse_options(argc, argv));
    }
    catch (const std::exception &exception)
    {
        std::cerr << "Voxel infrastructure experiment failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
