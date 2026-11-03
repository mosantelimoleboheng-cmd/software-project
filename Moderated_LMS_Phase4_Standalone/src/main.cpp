#include "lms.hpp"
#include <iostream>
int main() {
    try {
        lms::runApplication();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << '\n';
        return 1;
    }
}
