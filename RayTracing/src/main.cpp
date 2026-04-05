#include "Application.hpp"
#include <cstdlib>
#include <exception>

int main() {
    RYRayTracing::Application app;

    try {
        app.run();
        return EXIT_SUCCESS;
    } catch (const std::exception& e) {
        // Error should already be logged by the application
        return EXIT_FAILURE;
    }
}