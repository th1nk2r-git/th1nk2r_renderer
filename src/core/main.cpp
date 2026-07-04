#include <iostream>
#include "core/application.hpp"

int main(int argc, char **argv) {
    Application app;
    try {
        app.init();
        app.run();
    } 
    catch(const std::exception& ex) {
        std::cerr << ex.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
