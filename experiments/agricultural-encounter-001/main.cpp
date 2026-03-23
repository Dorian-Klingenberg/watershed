#include "console_ui.h"

#include <exception>
#include <iostream>

using namespace agricultural_encounter_001;

int main()
{
    try
    {
        run_console_ui();
        return 0;
    }
    catch (const std::exception &exception)
    {
        std::cerr << "Agricultural encounter experiment failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
