#include "matching_engine/router.hpp"

#include <charconv>
#include <csignal>
#include <iostream>
#include <pthread.h>
#include <stdexcept>
#include <string_view>

namespace {

template <typename T>
T positiveInteger(std::string_view text)
{
    T value{};
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || end != text.data() + text.size() || value <= 0) {
        throw std::invalid_argument{"port and stockId must be positive integers in range"};
    }
    return value;
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        if (argc > 4) {
            std::cerr << "Usage: matching_engine_server [address [port [stockId]]]\n";
            return 1;
        }
        matching_engine::RouterConfig config;
        if (argc > 1) { config.address = argv[1]; }
        if (argc > 2) { config.port = positiveInteger<std::uint16_t>(argv[2]); }
        if (argc > 3) { config.stockId = positiveInteger<matching_engine::StockId>(argv[3]); }

        // Linux/WSL: block signals before starting threads so main can wait without polling.
        sigset_t signals;
        sigemptyset(&signals);
        sigaddset(&signals, SIGINT);
        sigaddset(&signals, SIGTERM);
        if (pthread_sigmask(SIG_BLOCK, &signals, nullptr) != 0) {
            throw std::runtime_error{"Cannot configure shutdown signals"};
        }

        matching_engine::Router router{config};
        router.start();
        std::cout << "WebSocket router: ws://" << config.address << ':' << router.port()
                  << "/ (stockId=" << config.stockId << ")\n"
                  << "I/O thread + engine worker started. Ctrl+C to stop.\n" << std::flush;
        int signal = 0;
        if (sigwait(&signals, &signal) != 0) {
            throw std::runtime_error{"Cannot wait for shutdown signal"};
        }
        router.stop();
        std::cout << "Router stopped.\n";
    } catch (const std::exception& error) {
        std::cerr << "Server error: " << error.what() << '\n';
        return 1;
    }
}
