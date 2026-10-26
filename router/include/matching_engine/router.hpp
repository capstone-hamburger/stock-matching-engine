#pragma once

#include "matching_engine/models.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace matching_engine {

struct RouterConfig {
    std::string address{"127.0.0.1"};
    std::uint16_t port{9002}; // 0 lets the OS choose a port (useful in tests).
    StockId stockId{1001};
    std::size_t maxMessageBytes{16 * 1024};
    std::size_t maxConnections{128};
};

// Owns one WebSocket I/O thread and one engine worker thread.
// Call lifecycle methods from a single controlling thread, never from a handler.
class Router final {
public:
    explicit Router(RouterConfig config = {});
    ~Router();

    Router(const Router&) = delete;
    Router& operator=(const Router&) = delete;
    Router(Router&&) = delete;
    Router& operator=(Router&&) = delete;

    // Binds synchronously, then starts both threads. Throws on bind/start failure.
    // A Router is single-use: create a new instance to restart after stop().
    void start();
    // Stops input, drains already queued engine work, closes sockets and joins.
    // Responses in flight can be lost during shutdown. Repeated stop() is safe.
    void stop();
    [[nodiscard]] std::uint16_t port() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace matching_engine
