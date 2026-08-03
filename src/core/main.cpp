#include "core/application.hpp"
#include <iostream>

int main(int argc, char** argv) {
    try {
        Application app;
        app.run();
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
