#include "simulation.h"

#include <exception>
#include <iostream>

using namespace scalar_field_flooding;

int main()
{
    try
    {
        run_tests();
        std::cout << "Scalar field flooding tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Scalar field flooding tests failed: " << exception.what() << '\n';
        return 1;
    }
}
