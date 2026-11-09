#pragma once

#include "matching_engine/engine.hpp"

#include <boost/asio/steady_timer.hpp>

#include <chrono>
#include <functional>

namespace matching_engine {

// The engine worker serializes book updates and timer callbacks.
class MarketDataPublisher final {
public:
    using Publish = std::function<void(StockId, const OrderBookSnapshot&)>;

    MarketDataPublisher(boost::asio::io_context& context, const Engine& engine,
        std::chrono::milliseconds interval, Publish publish);
    void start();
    void stop();
    void refresh();

private:
    void scheduleTick();

    boost::asio::steady_timer timer_;
    const Engine& engine_;
    std::chrono::milliseconds interval_;
    Publish publish_;
    OrderBookSnapshot snapshot_;
    bool running_{false};
};

} // namespace matching_engine
