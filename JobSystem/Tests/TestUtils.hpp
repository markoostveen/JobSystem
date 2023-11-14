#pragma once

#include <iostream>

inline void DoStuff()
{
    std::cout << "Doing stuff during execution" << std::endl;

    std::this_thread::sleep_for(std::chrono::nanoseconds(100));

    std::cout << "Doing stuff during execution" << std::endl;
}