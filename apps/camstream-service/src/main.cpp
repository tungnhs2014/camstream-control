#include "camstream/camera_service.hpp"

#include <iostream>
#include <string_view>

namespace {

constexpr int kRuntimeErrorExitCode = 1;
constexpr int kArgumentErrorExitCode = 2;

void print_usage(std::ostream& output, const char* program)
{
    output << "Usage: " << program << " [--help]\n\n"
           << "Run the CamStream camera service in the foreground.\n";
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        print_usage(std::cout, argv[0]);
        return 0;
    }
    if (argc != 1) {
        std::cerr << "Error: unexpected argument";
        if (argc > 1) {
            std::cerr << " '" << argv[1] << '\'';
        }
        std::cerr << '\n';
        print_usage(std::cerr, argv[0]);
        return kArgumentErrorExitCode;
    }

    std::cout << "Camera service starting" << std::endl;
    camstream::CameraService service;
    if (!service.initialize()) {
        static_cast<void>(service.shutdown());
        return kRuntimeErrorExitCode;
    }

    const bool run_succeeded = service.run();
    const bool shutdown_succeeded = service.shutdown();
    if (!run_succeeded || !shutdown_succeeded) {
        return kRuntimeErrorExitCode;
    }
    return 0;
}
