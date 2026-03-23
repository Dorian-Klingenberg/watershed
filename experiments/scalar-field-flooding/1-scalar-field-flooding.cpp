#include "simulation.h"
#include "map_generation.h"
#include "ui.h"

#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>

using namespace scalar_field_flooding;

int main()
{
    try
    {
        GridState grid = make_manual_test_map();
        const std::filesystem::path export_dir = "experiments/scalar-field-flooding/exports";

        constexpr int step_count = 200;
        for (int step = 0; step <= step_count; ++step)
        {
            print_step_report(grid, step);

            if (step % 5 == 0)
            {
                std::ostringstream filename;
                filename << "step-" << std::setw(4) << std::setfill('0') << step << ".bmp";
                write_bmp_snapshot(grid, export_dir / filename.str());
            }

            grid = simulate_step(grid);
        }

        return 0;
    }
    catch (const std::exception &exception)
    {
        std::cerr << "Experiment failed: " << exception.what() << '\n';
        return 1;
    }
}
