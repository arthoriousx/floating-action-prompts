#include "check.h"
#include <iostream>
#include <string_view>
#include "../src/Version.h"

int main()
{
    std::cout << "[TEST] Running test_scaffold..." << std::endl;
    CHECK(std::string_view(Plugin::NAME) == "FloatingActionPrompts");
    CHECK(std::string_view(Plugin::AUTHOR) == "ArthoriousX");
    CHECK(Plugin::VERSION_MAJOR == 2);
    CHECK(Plugin::VERSION_MINOR == 0);
    CHECK(Plugin::VERSION_PATCH == 0);
    CHECK(std::string_view(Plugin::VERSION_STRING) == "2.0.0");
    std::cout << "[TEST] test_scaffold PASSED" << std::endl;
    return 0;
}
