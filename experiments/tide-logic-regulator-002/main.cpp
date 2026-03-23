#include "dashboard.h"

#include <exception>
#include <iostream>
#include <string>

using namespace tide_logic_regulator_002;

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
            std::cout << "Usage: tide_logic_regulator_002_experiment [--ticks N] [--sleep-ms N] [--no-clear] [--non-interactive]\n";
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
        std::cerr << "Tide logic regulator experiment failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
 