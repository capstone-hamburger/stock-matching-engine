#include "matching_engine/engine.hpp"

#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

using matching_engine::CancellationFailureReason;
using matching_engine::Engine;
using matching_engine::OrderRequest;
using matching_engine::OrderStatus;
using matching_engine::OrderType;
using matching_engine::SubmissionRejectionReason;

class TestContext final {
public:
    void expect(bool condition, std::string_view message)
    {
        if (!condition) {
            ++failureCount_;
            std::cerr << "FAIL: " << message << '\n';
        }
    }

    [[nodiscard]] int failureCount() const noexcept
    {
        return failureCount_;
    }

private:
    int failureCount_{0};
};

[[nodiscard]] OrderRequest request(
    matching_engine::OrderId orderId,
    matching_engine::UserId userId,
    OrderType type,
    matching_engine::Price price,
    matching_engine::Quantity quantity)
{
    return {orderId, userId, type, price, quantity};
}

void testValidationAndIdPolicy(TestContext& test)
{
    bool threw = false;
    try {
        Engine invalidEngine{0};
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    test.expect(threw, "non-positive stockId must be rejected");

    Engine engine{101};
    test.expect(engine.stockId() == 101, "engine must retain its stockId");

    const auto expectRejected = [&test, &engine](
                                    const OrderRequest& value,
                                    SubmissionRejectionReason reason) {
        const auto result = engine.submit(value);
        test.expect(!result.accepted, "invalid order must not be accepted");
        test.expect(result.rejectionReason == reason, "invalid order reason must match");
        test.expect(result.status == OrderStatus::Rejected, "invalid order status must be rejected");
        test.expect(result.filledQuantity == 0 && result.remainingQuantity == 0,
            "invalid order quantities must be zero");
        test.expect(result.trades.empty(), "invalid order must not create trades");
    };

    expectRejected(request(0, 1, OrderType::Buy, 100, 1),
        SubmissionRejectionReason::InvalidOrderId);
    expectRejected(request(1, 0, OrderType::Buy, 100, 1),
        SubmissionRejectionReason::InvalidUserId);
    expectRejected(request(2, 1, static_cast<OrderType>(99), 100, 1),
        SubmissionRejectionReason::InvalidOrderType);
    expectRejected(request(3, 1, OrderType::Buy, 0, 1),
        SubmissionRejectionReason::InvalidPrice);
    expectRejected(request(4, 1, OrderType::Buy, matching_engine::maxPrice + 1, 1),
        SubmissionRejectionReason::InvalidPrice);
    expectRejected(request(5, 1, OrderType::Buy, 100, 0),
        SubmissionRejectionReason::InvalidQuantity);
    expectRejected(request(6, 1, OrderType::Buy, 100, matching_engine::maxOrderQuantity + 1),
        SubmissionRejectionReason::InvalidQuantity);

    test.expect(engine.orderBook().bids.empty() && engine.orderBook().asks.empty(),
        "rejections must leave the book unchanged");
    test.expect(!engine.lastTradePrice().has_value(),
        "rejections must leave last trade price empty");

    const auto corrected = engine.submit(request(6, 1, OrderType::Buy, 100, 5));
    test.expect(corrected.accepted, "an ID from a rejected order may be reused");
    const auto canceled = engine.cancel(6, 1);
    test.expect(canceled.success && canceled.canceledQuantity == 5,
        "accepted order must be cancellable");

    expectRejected(request(6, 1, OrderType::Buy, 100, 5),
        SubmissionRejectionReason::DuplicateOrderId);
}

void testBuyPriceTimePriorityAndContinuousMatching(TestContext& test)
{
    Engine engine{202};

    test.expect(engine.submit(request(10, 2, OrderType::Sell, 102, 3)).accepted,
        "first ask must be accepted");
    test.expect(engine.submit(request(11, 3, OrderType::Sell, 101, 2)).accepted,
        "second ask must be accepted");
    test.expect(engine.submit(request(12, 4, OrderType::Sell, 101, 4)).accepted,
        "third ask must be accepted");

    const auto before = engine.orderBook();
    test.expect(before.asks.size() == 2, "asks must aggregate into two price levels");
    test.expect(before.asks[0].price == 101 && before.asks[0].quantity == 6,
        "lowest ask must be first and aggregate FIFO orders");
    test.expect(before.asks[1].price == 102 && before.asks[1].quantity == 3,
        "higher ask must follow");
    test.expect(!engine.lastTradePrice().has_value(),
        "resting orders must not set a last trade price");

    const auto result = engine.submit(request(20, 5, OrderType::Buy, 102, 8));
    test.expect(result.accepted && result.status == OrderStatus::Filled,
        "crossing buy must fill completely");
    test.expect(result.filledQuantity == 8 && result.remainingQuantity == 0,
        "buy fill quantities must match");
    test.expect(result.trades.size() == 3, "buy must match three resting orders");

    if (result.trades.size() == 3) {
        test.expect(result.trades[0].tradeId == 1
                && result.trades[0].sellOrderId == 11
                && result.trades[0].price == 101
                && result.trades[0].quantity == 2,
            "lowest ask and earlier FIFO order must trade first");
        test.expect(result.trades[1].tradeId == 2
                && result.trades[1].sellOrderId == 12
                && result.trades[1].price == 101
                && result.trades[1].quantity == 4,
            "second FIFO order at the same price must trade second");
        test.expect(result.trades[2].tradeId == 3
                && result.trades[2].sellOrderId == 10
                && result.trades[2].price == 102
                && result.trades[2].quantity == 2,
            "next price level must trade last at resting price");
    }

    const auto partiallyFilled = engine.order(10);
    test.expect(partiallyFilled.has_value(), "resting order must remain queryable");
    if (partiallyFilled) {
        test.expect(partiallyFilled->status == OrderStatus::PartiallyFilled
                && partiallyFilled->filledQuantity == 2
                && partiallyFilled->remainingQuantity == 1,
            "resting ask must retain its partial remainder");
    }

    const auto after = engine.orderBook();
    test.expect(after.bids.empty(), "filled incoming buy must not rest");
    test.expect(after.asks.size() == 1
            && after.asks[0].price == 102
            && after.asks[0].quantity == 1,
        "only the unmatched ask remainder must remain");
    test.expect(engine.lastTradePrice() == 102,
        "last trade price must equal the final execution price");
}

void testSellMatchesHighestBidAtRestingPrices(TestContext& test)
{
    Engine engine{303};
    static_cast<void>(engine.submit(request(1, 10, OrderType::Buy, 99, 3)));
    static_cast<void>(engine.submit(request(2, 20, OrderType::Buy, 100, 2)));
    static_cast<void>(engine.submit(request(3, 30, OrderType::Buy, 100, 4)));

    const auto before = engine.orderBook();
    test.expect(before.bids.size() == 2, "bids must aggregate into two price levels");
    test.expect(before.bids[0].price == 100 && before.bids[0].quantity == 6,
        "highest bid must be first and aggregate FIFO orders");
    test.expect(before.bids[1].price == 99 && before.bids[1].quantity == 3,
        "lower bid must follow");

    const auto result = engine.submit(request(4, 40, OrderType::Sell, 98, 10));
    test.expect(result.status == OrderStatus::PartiallyFilled,
        "oversized sell must remain partially filled");
    test.expect(result.filledQuantity == 9 && result.remainingQuantity == 1,
        "sell quantities must include all crossed bids");
    test.expect(result.trades.size() == 3, "sell must match all three bid orders");

    if (result.trades.size() == 3) {
        test.expect(result.trades[0].buyOrderId == 2 && result.trades[0].price == 100,
            "highest and earliest bid must trade first");
        test.expect(result.trades[1].buyOrderId == 3 && result.trades[1].price == 100,
            "same-price later bid must trade second");
        test.expect(result.trades[2].buyOrderId == 1 && result.trades[2].price == 99,
            "lower bid must trade after higher bids");
    }

    const auto book = engine.orderBook();
    test.expect(book.bids.empty(), "all crossed bids must be removed");
    test.expect(book.asks.size() == 1
            && book.asks[0].price == 98
            && book.asks[0].quantity == 1,
        "incoming sell remainder must rest at its limit price");
    test.expect(engine.lastTradePrice() == 99,
        "sell must update last price to final resting bid price");
}

void testCancellationAndLastPrice(TestContext& test)
{
    Engine engine{404};
    static_cast<void>(engine.submit(request(1, 10, OrderType::Buy, 100, 10)));
    const auto fill = engine.submit(request(2, 20, OrderType::Sell, 100, 4));
    test.expect(fill.status == OrderStatus::Filled, "small sell must fill");
    test.expect(engine.lastTradePrice() == 100, "trade must set the last price");

    test.expect(engine.cancel(0, 10).failureReason == CancellationFailureReason::InvalidOrderId,
        "invalid cancel order ID must be reported");
    test.expect(engine.cancel(1, 0).failureReason == CancellationFailureReason::InvalidUserId,
        "invalid cancel user ID must be reported");
    test.expect(engine.cancel(999, 10).failureReason == CancellationFailureReason::OrderNotFound,
        "unknown order must be reported");
    test.expect(engine.cancel(1, 99).failureReason == CancellationFailureReason::NotOwner,
        "another user must not cancel the order");
    test.expect(engine.cancel(2, 20).failureReason == CancellationFailureReason::AlreadyFilled,
        "filled order must not be canceled");
    const auto filledDuplicate = engine.submit(request(2, 20, OrderType::Sell, 99, 1));
    test.expect(filledDuplicate.rejectionReason == SubmissionRejectionReason::DuplicateOrderId,
        "filled order ID must never be reusable");

    const auto canceled = engine.cancel(1, 10);
    test.expect(canceled.success && canceled.canceledQuantity == 6,
        "partial order cancel must remove only its remainder");
    test.expect(engine.cancel(1, 10).failureReason == CancellationFailureReason::AlreadyCanceled,
        "second cancel must report an already canceled order");

    const auto order = engine.order(1);
    test.expect(order.has_value(), "canceled order must remain queryable");
    if (order) {
        test.expect(order->status == OrderStatus::Canceled
                && order->originalQuantity == 10
                && order->filledQuantity == 4
                && order->canceledQuantity == 6
                && order->remainingQuantity == 0,
            "canceled order snapshot must preserve fill and cancel quantities");
    }

    test.expect(engine.orderBook().bids.empty() && engine.orderBook().asks.empty(),
        "canceled remainder must leave the book");
    test.expect(engine.lastTradePrice() == 100,
        "cancel must not change the last trade price");

    const auto duplicate = engine.submit(request(1, 10, OrderType::Buy, 101, 1));
    test.expect(duplicate.rejectionReason == SubmissionRejectionReason::DuplicateOrderId,
        "canceled order ID must never be reusable");

    Engine noTradeEngine{405};
    static_cast<void>(noTradeEngine.submit(request(10, 1, OrderType::Buy, 50, 2)));
    static_cast<void>(noTradeEngine.cancel(10, 1));
    test.expect(!noTradeEngine.lastTradePrice().has_value(),
        "non-trading submit and cancel must leave last price empty");
}

} // namespace

int main()
{
    TestContext test;
    testValidationAndIdPolicy(test);
    testBuyPriceTimePriorityAndContinuousMatching(test);
    testSellMatchesHighestBidAtRestingPrices(test);
    testCancellationAndLastPrice(test);

    if (test.failureCount() != 0) {
        std::cerr << test.failureCount() << " test assertion(s) failed\n";
        return 1;
    }

    std::cout << "all matching engine tests passed\n";
    return 0;
}
