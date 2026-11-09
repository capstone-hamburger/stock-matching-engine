#include "market_data_publisher.hpp"

#include <stdexcept>
#include <utility>

namespace matching_engine {

MarketDataPublisher::MarketDataPublisher(boost::asio::io_context& context, const Engine& engine,
    std::chrono::milliseconds interval, Publish publish)
    : timer_(context), engine_(engine), interval_(interval), publish_(std::move(publish))
{
    if (interval_ <= std::chrono::milliseconds::zero()) {
        throw std::invalid_argument{"Market data interval must be positive"};
    }
}

void MarketDataPublisher::refresh()
{
    snapshot_ = engine_.orderBook(5);
}

void MarketDataPublisher::start()
{
    refresh();
    running_ = true;
    timer_.expires_after(interval_);
    scheduleTick();
}

void MarketDataPublisher::scheduleTick()
{
    timer_.async_wait([this](boost::system::error_code ec) {
        if (!ec && running_) {
            publish_(engine_.stockId(), snapshot_);
            const auto nextTick = timer_.expiry() + interval_;
            const auto now = std::chrono::steady_clock::now();
            timer_.expires_at(nextTick > now ? nextTick : now + interval_);
            scheduleTick();
        }
    });
}

void MarketDataPublisher::stop()
{
    running_ = false;
    timer_.cancel();
}

} // namespace matching_engine
