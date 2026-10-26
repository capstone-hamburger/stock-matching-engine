#pragma once

#include "matching_engine/models.hpp"

#include <memory>
#include <optional>

namespace matching_engine {

class Engine final {
public:
    explicit Engine(StockId stockId);
    ~Engine();

    Engine(Engine&&) noexcept;
    Engine& operator=(Engine&&) noexcept;

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    [[nodiscard]] StockId stockId() const noexcept;
    [[nodiscard]] SubmissionResult submit(const OrderRequest& request);
    [[nodiscard]] CancellationResult cancel(OrderId orderId, UserId requestingUserId);
    [[nodiscard]] std::optional<OrderSnapshot> order(OrderId orderId) const;
    [[nodiscard]] OrderBookSnapshot orderBook() const;
    [[nodiscard]] std::optional<Price> lastTradePrice() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace matching_engine
