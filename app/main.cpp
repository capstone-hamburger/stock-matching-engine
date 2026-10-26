#include "matching_engine/engine.hpp"

#include <iostream>

namespace {

void printSubmission(
    const matching_engine::OrderRequest& request,
    const matching_engine::SubmissionResult& result)
{
    std::cout << "order " << request.orderId << ' ' << matching_engine::toString(request.type)
              << " -> " << matching_engine::toString(result.status)
              << ", filled=" << result.filledQuantity
              << ", remaining=" << result.remainingQuantity << '\n';

    for (const auto& trade : result.trades) {
        std::cout << "  trade " << trade.tradeId
                  << ": buy=" << trade.buyOrderId
                  << ", sell=" << trade.sellOrderId
                  << ", price=" << trade.price
                  << ", quantity=" << trade.quantity << '\n';
    }
}

void printBook(const matching_engine::OrderBookSnapshot& book)
{
    std::cout << "bids:";
    for (const auto& level : book.bids) {
        std::cout << " [" << level.price << " x " << level.quantity << ']';
    }
    std::cout << "\nasks:";
    for (const auto& level : book.asks) {
        std::cout << " [" << level.price << " x " << level.quantity << ']';
    }
    std::cout << '\n';
}

} // namespace

int main()
{
    using matching_engine::OrderRequest;
    using matching_engine::OrderType;

    matching_engine::Engine engine{1001};
    std::cout << "stock " << engine.stockId() << " matching demo\n";

    const OrderRequest firstSell{
        .orderId = 2001,
        .userId = 20,
        .type = OrderType::Sell,
        .price = 10'100,
        .quantity = 7,
    };
    const OrderRequest secondSell{
        .orderId = 2002,
        .userId = 30,
        .type = OrderType::Sell,
        .price = 10'200,
        .quantity = 5,
    };
    const OrderRequest buy{
        .orderId = 1001,
        .userId = 10,
        .type = OrderType::Buy,
        .price = 10'200,
        .quantity = 10,
    };

    printSubmission(firstSell, engine.submit(firstSell));
    printSubmission(secondSell, engine.submit(secondSell));
    printSubmission(buy, engine.submit(buy));
    printBook(engine.orderBook());

    const auto cancellation = engine.cancel(secondSell.orderId, secondSell.userId);
    std::cout << "cancel order " << secondSell.orderId
              << " -> " << (cancellation.success ? "SUCCESS" : "FAILED")
              << ", canceled=" << cancellation.canceledQuantity << '\n';
    printBook(engine.orderBook());

    if (const auto lastPrice = engine.lastTradePrice()) {
        std::cout << "last trade price: " << *lastPrice << '\n';
    } else {
        std::cout << "last trade price: none\n";
    }
}
