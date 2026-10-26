#include "matching_engine/engine.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace matching_engine {

namespace {

struct OrderRecord {
    OrderId orderId;
    UserId userId;
    OrderType type;
    Price price;
    Quantity originalQuantity;
    Quantity filledQuantity{0};
    Quantity canceledQuantity{0};
    Quantity remainingQuantity;
    OrderStatus status{OrderStatus::Open};
};

using BidBook = std::map<Price, std::deque<OrderId>, std::greater<Price>>;
using AskBook = std::map<Price, std::deque<OrderId>, std::less<Price>>;

[[nodiscard]] bool isValidOrderType(OrderType type) noexcept
{
    return type == OrderType::Buy || type == OrderType::Sell;
}

[[nodiscard]] SubmissionResult reject(SubmissionRejectionReason reason)
{
    return SubmissionResult{
        .accepted = false,
        .rejectionReason = reason,
        .status = OrderStatus::Rejected,
        .filledQuantity = 0,
        .remainingQuantity = 0,
        .trades = {},
    };
}

[[nodiscard]] OrderSnapshot snapshot(const OrderRecord& order)
{
    return OrderSnapshot{
        .orderId = order.orderId,
        .userId = order.userId,
        .type = order.type,
        .price = order.price,
        .originalQuantity = order.originalQuantity,
        .filledQuantity = order.filledQuantity,
        .canceledQuantity = order.canceledQuantity,
        .remainingQuantity = order.remainingQuantity,
        .status = order.status,
    };
}

} // namespace

class Engine::Impl final {
public:
    explicit Impl(StockId stockId)
        : stockId_(stockId)
    {
        if (stockId <= 0) {
            throw std::invalid_argument{"stockId must be positive"};
        }
    }

    [[nodiscard]] StockId stockId() const noexcept
    {
        return stockId_;
    }

    [[nodiscard]] SubmissionResult submit(const OrderRequest& request)
    {
        const auto validationFailure = validate(request);
        if (validationFailure != SubmissionRejectionReason::None) {
            return reject(validationFailure);
        }

        const MatchPreview preview = previewMatch(request);
        if (!hasTradeIdCapacity(preview.tradeCount)
            || !hasBookQuantityCapacity(request.type, request.price, preview.remainingQuantity)) {
            return reject(SubmissionRejectionReason::EngineCapacityExceeded);
        }

        auto [incomingIterator, inserted] = orders_.emplace(
            request.orderId,
            OrderRecord{
                .orderId = request.orderId,
                .userId = request.userId,
                .type = request.type,
                .price = request.price,
                .originalQuantity = request.quantity,
                .remainingQuantity = request.quantity,
            });
        static_cast<void>(inserted);

        OrderRecord& incoming = incomingIterator->second;
        std::vector<Trade> trades;
        trades.reserve(static_cast<std::size_t>(preview.tradeCount));

        if (request.type == OrderType::Buy) {
            matchBuy(incoming, trades);
        } else {
            matchSell(incoming, trades);
        }

        if (incoming.remainingQuantity == 0) {
            incoming.status = OrderStatus::Filled;
        } else {
            incoming.status = incoming.filledQuantity == 0 ? OrderStatus::Open
                                                           : OrderStatus::PartiallyFilled;
            addToBook(incoming);
        }

        return SubmissionResult{
            .accepted = true,
            .rejectionReason = SubmissionRejectionReason::None,
            .status = incoming.status,
            .filledQuantity = incoming.filledQuantity,
            .remainingQuantity = incoming.remainingQuantity,
            .trades = std::move(trades),
        };
    }

    [[nodiscard]] CancellationResult cancel(OrderId orderId, UserId requestingUserId)
    {
        if (orderId <= 0) {
            return {.failureReason = CancellationFailureReason::InvalidOrderId};
        }
        if (requestingUserId <= 0) {
            return {.failureReason = CancellationFailureReason::InvalidUserId};
        }

        const auto iterator = orders_.find(orderId);
        if (iterator == orders_.end()) {
            return {.failureReason = CancellationFailureReason::OrderNotFound};
        }

        OrderRecord& order = iterator->second;
        if (order.userId != requestingUserId) {
            return {.failureReason = CancellationFailureReason::NotOwner};
        }
        if (order.status == OrderStatus::Filled) {
            return {.failureReason = CancellationFailureReason::AlreadyFilled};
        }
        if (order.status == OrderStatus::Canceled) {
            return {.failureReason = CancellationFailureReason::AlreadyCanceled};
        }

        removeFromBook(order);
        const Quantity canceledQuantity = order.remainingQuantity;
        order.canceledQuantity += canceledQuantity;
        order.remainingQuantity = 0;
        order.status = OrderStatus::Canceled;

        return {
            .success = true,
            .canceledQuantity = canceledQuantity,
            .failureReason = CancellationFailureReason::None,
        };
    }

    [[nodiscard]] std::optional<OrderSnapshot> order(OrderId orderId) const
    {
        const auto iterator = orders_.find(orderId);
        if (iterator == orders_.end()) {
            return std::nullopt;
        }
        return snapshot(iterator->second);
    }

    [[nodiscard]] OrderBookSnapshot orderBook() const
    {
        OrderBookSnapshot result;
        result.bids.reserve(bids_.size());
        result.asks.reserve(asks_.size());

        for (const auto& [price, queue] : bids_) {
            result.bids.push_back({price, levelQuantity(queue)});
        }
        for (const auto& [price, queue] : asks_) {
            result.asks.push_back({price, levelQuantity(queue)});
        }
        return result;
    }

    [[nodiscard]] std::optional<Price> lastTradePrice() const noexcept
    {
        return lastTradePrice_;
    }

private:
    struct MatchPreview {
        Quantity remainingQuantity;
        Quantity tradeCount;
    };

    [[nodiscard]] SubmissionRejectionReason validate(const OrderRequest& request) const
    {
        if (request.orderId <= 0) {
            return SubmissionRejectionReason::InvalidOrderId;
        }
        if (request.userId <= 0) {
            return SubmissionRejectionReason::InvalidUserId;
        }
        if (!isValidOrderType(request.type)) {
            return SubmissionRejectionReason::InvalidOrderType;
        }
        if (request.price <= 0 || request.price > maxPrice) {
            return SubmissionRejectionReason::InvalidPrice;
        }
        if (request.quantity <= 0 || request.quantity > maxOrderQuantity) {
            return SubmissionRejectionReason::InvalidQuantity;
        }
        if (orders_.contains(request.orderId)) {
            return SubmissionRejectionReason::DuplicateOrderId;
        }
        return SubmissionRejectionReason::None;
    }

    [[nodiscard]] MatchPreview previewMatch(const OrderRequest& request) const
    {
        MatchPreview result{request.quantity, 0};

        const auto visitQueue = [this, &result](const std::deque<OrderId>& queue) {
            for (const OrderId orderId : queue) {
                if (result.remainingQuantity == 0) {
                    break;
                }
                const OrderRecord& resting = orders_.at(orderId);
                const Quantity matched = std::min(result.remainingQuantity, resting.remainingQuantity);
                result.remainingQuantity -= matched;
                ++result.tradeCount;
            }
        };

        if (request.type == OrderType::Buy) {
            for (const auto& [price, queue] : asks_) {
                if (price > request.price || result.remainingQuantity == 0) {
                    break;
                }
                visitQueue(queue);
            }
        } else {
            for (const auto& [price, queue] : bids_) {
                if (price < request.price || result.remainingQuantity == 0) {
                    break;
                }
                visitQueue(queue);
            }
        }
        return result;
    }

    [[nodiscard]] bool hasTradeIdCapacity(Quantity tradeCount) const noexcept
    {
        if (tradeCount == 0) {
            return true;
        }
        if (!nextTradeId_.has_value()) {
            return false;
        }

        const auto available = static_cast<std::uint64_t>(
            std::numeric_limits<TradeId>::max() - *nextTradeId_)
            + std::uint64_t{1};
        return static_cast<std::uint64_t>(tradeCount) <= available;
    }

    [[nodiscard]] bool hasBookQuantityCapacity(
        OrderType type,
        Price price,
        Quantity quantity) const
    {
        if (quantity == 0) {
            return true;
        }

        Quantity current = 0;
        if (type == OrderType::Buy) {
            const auto iterator = bids_.find(price);
            if (iterator != bids_.end()) {
                current = levelQuantity(iterator->second);
            }
        } else {
            const auto iterator = asks_.find(price);
            if (iterator != asks_.end()) {
                current = levelQuantity(iterator->second);
            }
        }

        return current <= std::numeric_limits<Quantity>::max() - quantity;
    }

    [[nodiscard]] Quantity levelQuantity(const std::deque<OrderId>& queue) const
    {
        Quantity total = 0;
        for (const OrderId orderId : queue) {
            total += orders_.at(orderId).remainingQuantity;
        }
        return total;
    }

    void matchBuy(OrderRecord& incoming, std::vector<Trade>& trades)
    {
        while (incoming.remainingQuantity > 0 && !asks_.empty()) {
            auto level = asks_.begin();
            if (level->first > incoming.price) {
                break;
            }

            auto& queue = level->second;
            while (incoming.remainingQuantity > 0 && !queue.empty()) {
                OrderRecord& resting = orders_.at(queue.front());
                executeTrade(incoming, resting, trades);
                if (resting.remainingQuantity == 0) {
                    queue.pop_front();
                }
            }

            if (queue.empty()) {
                asks_.erase(level);
            }
        }
    }

    void matchSell(OrderRecord& incoming, std::vector<Trade>& trades)
    {
        while (incoming.remainingQuantity > 0 && !bids_.empty()) {
            auto level = bids_.begin();
            if (level->first < incoming.price) {
                break;
            }

            auto& queue = level->second;
            while (incoming.remainingQuantity > 0 && !queue.empty()) {
                OrderRecord& resting = orders_.at(queue.front());
                executeTrade(incoming, resting, trades);
                if (resting.remainingQuantity == 0) {
                    queue.pop_front();
                }
            }

            if (queue.empty()) {
                bids_.erase(level);
            }
        }
    }

    void executeTrade(OrderRecord& incoming, OrderRecord& resting, std::vector<Trade>& trades)
    {
        const Quantity tradedQuantity = std::min(
            incoming.remainingQuantity,
            resting.remainingQuantity);
        const TradeId tradeId = *nextTradeId_;

        trades.push_back(Trade{
            .tradeId = tradeId,
            .buyOrderId = incoming.type == OrderType::Buy ? incoming.orderId : resting.orderId,
            .sellOrderId = incoming.type == OrderType::Sell ? incoming.orderId : resting.orderId,
            .price = resting.price,
            .quantity = tradedQuantity,
        });

        if (tradeId == std::numeric_limits<TradeId>::max()) {
            nextTradeId_ = std::nullopt;
        } else {
            nextTradeId_ = tradeId + 1;
        }

        incoming.filledQuantity += tradedQuantity;
        incoming.remainingQuantity -= tradedQuantity;
        resting.filledQuantity += tradedQuantity;
        resting.remainingQuantity -= tradedQuantity;
        resting.status = resting.remainingQuantity == 0 ? OrderStatus::Filled
                                                        : OrderStatus::PartiallyFilled;
        lastTradePrice_ = resting.price;
    }

    void addToBook(const OrderRecord& order)
    {
        if (order.type == OrderType::Buy) {
            bids_[order.price].push_back(order.orderId);
        } else {
            asks_[order.price].push_back(order.orderId);
        }
    }

    void removeFromBook(const OrderRecord& order)
    {
        const auto removeOrderId = [&order](auto& book) {
            const auto level = book.find(order.price);
            if (level == book.end()) {
                return;
            }

            auto& queue = level->second;
            const auto position = std::find(queue.begin(), queue.end(), order.orderId);
            if (position != queue.end()) {
                queue.erase(position);
            }
            if (queue.empty()) {
                book.erase(level);
            }
        };

        if (order.type == OrderType::Buy) {
            removeOrderId(bids_);
        } else {
            removeOrderId(asks_);
        }
    }

    StockId stockId_;
    std::unordered_map<OrderId, OrderRecord> orders_;
    BidBook bids_;
    AskBook asks_;
    std::optional<Price> lastTradePrice_;
    std::optional<TradeId> nextTradeId_{TradeId{1}};
};

Engine::Engine(StockId stockId)
    : impl_(std::make_unique<Impl>(stockId))
{
}

Engine::~Engine() = default;
Engine::Engine(Engine&&) noexcept = default;
Engine& Engine::operator=(Engine&&) noexcept = default;

StockId Engine::stockId() const noexcept
{
    return impl_->stockId();
}

SubmissionResult Engine::submit(const OrderRequest& request)
{
    return impl_->submit(request);
}

CancellationResult Engine::cancel(OrderId orderId, UserId requestingUserId)
{
    return impl_->cancel(orderId, requestingUserId);
}

std::optional<OrderSnapshot> Engine::order(OrderId orderId) const
{
    return impl_->order(orderId);
}

OrderBookSnapshot Engine::orderBook() const
{
    return impl_->orderBook();
}

std::optional<Price> Engine::lastTradePrice() const noexcept
{
    return impl_->lastTradePrice();
}

} // namespace matching_engine
