# 매칭 엔진 MVP 명세

## 1. 목적과 경계

`matching_engine::Engine` 인스턴스 하나는 생성 시 받은 `stockId` 한 종목의 호가창을 관리한다. 엔진은 지정가 주문 접수, 가격-시간 우선 매칭, 잔량 취소, 주문·호가 조회와 최근 체결가 조회만 담당한다.

엔진은 내부 스레드를 만들지 않으며 같은 인스턴스의 메서드는 호출자가 순차 호출해야 한다. DB, 네트워크, 외부 시세, 봇, 방 스케줄링, 인증, 잔고 검증, 자산 예약과 체결 후 계좌 정산은 엔진 밖의 책임이다.

## 2. 데이터 모델과 범위

DB 초안의 이름과 정수 타입을 반영해 다음 용어를 사용한다.

| 개념 | C++ 이름 | 범위 |
| --- | --- | --- |
| 종목 ID | `StockId`, `stockId` | 양수 `int64` |
| 주문 ID | `OrderId`, `orderId` | 양수 `int64` |
| 참가자 ID | `UserId`, `userId` | 양수 `int64` |
| 체결 ID | `TradeId`, `tradeId` | 엔진별 1부터 증가하는 양수 `int64` |
| 주문 종류 | `OrderType`, `type` | `Buy` 또는 `Sell` |
| 지정가 | `Price`, `price` | 1 이상 `INT32_MAX` 이하의 최소 가격 단위 정수 |
| 주문수량 | `Quantity`, `quantity` | 1 이상 `INT32_MAX` 이하의 정수 |

`price`는 원화 금액 자체가 아니라 시스템이 정한 최소 가격 단위의 개수다. 이 엔진은 `price * quantity` 금액 계산을 하지 않으며, 금액과 잔고의 범위 검사는 외부 정산 계층이 담당한다.

생성자의 `stockId`가 양수가 아니면 `std::invalid_argument`를 던진다. 주문 ID와 사용자 ID, 주문 종류, 가격, 수량, 내부 수량 합계와 체결 ID 여유를 실제 상태 변경 전에 검증한다.

## 3. 주문 접수와 ID 정책

`submit(OrderRequest)`는 다음 순서로 입력을 검증한다.

1. `orderId`와 `userId`가 양수인지 확인한다.
2. `type`이 `Buy` 또는 `Sell`인지 확인한다.
3. `price`와 `quantity`가 허용 범위인지 확인한다.
4. 이전에 접수된 `orderId`인지 확인한다.
5. 호가 잔량 합계나 체결 ID에서 산술 오버플로가 발생할 수 있는지 확인한다.

거절 결과는 `accepted=false`, `status=Rejected`, 체결량과 잔량은 모두 0이며 체결 목록은 비어 있다. 거절은 호가창, 주문 이력, 최근 체결가와 ID 카운터를 변경하지 않는다.

엔진 수명 동안 한 번이라도 **접수된** 주문 ID는 주문이 전량 체결되거나 취소된 뒤에도 재사용할 수 없다. 입력 단계에서 거절된 주문은 접수 이력에 남지 않으므로 같은 ID를 수정된 입력에 다시 사용할 수 있다.

## 4. 매칭 규칙

- 매수 주문은 지정가 이하의 매도 주문 중 가장 낮은 가격부터 체결한다.
- 매도 주문은 지정가 이상의 매수 주문 중 가장 높은 가격부터 체결한다.
- 같은 가격에서는 엔진에 먼저 접수되어 대기한 주문을 먼저 체결한다.
- 체결 가격은 항상 기존 호가창에서 대기하던 주문의 가격이다.
- 하나의 신규 주문은 잔량이 0이 되거나 더 이상 가격이 교차하지 않을 때까지 여러 대기 주문과 연속 체결한다.
- 신규 주문의 미체결 잔량은 원래 지정가의 FIFO 큐 끝에 들어간다.

각 체결은 엔진 안에서 유일한 순차 `tradeId`, 매수 주문 ID, 매도 주문 ID, 체결 가격과 체결 수량을 갖는다. 주문 접수 결과의 체결 목록은 실제 발생 순서대로 반환한다.

자기체결 방지 정책은 아직 정해지지 않았다. 현재 구현은 같은 `userId` 사이의 교차 주문도 체결할 수 있으므로 자기체결 방지가 구현 완료된 것으로 간주하면 안 되며, 데모의 정상 주문은 서로 다른 사용자 ID를 사용한다.

## 5. 주문 상태

| 상태 | 의미 |
| --- | --- |
| `Open` | 한 번도 체결되지 않았고 잔량 전체가 대기 중 |
| `PartiallyFilled` | 일부가 체결되고 잔량이 대기 중 |
| `Filled` | 주문수량 전체가 체결됨 |
| `Canceled` | 남은 활성 잔량이 취소됨 |
| `Rejected` | 접수 결과 전용 상태이며 주문 이력에는 저장되지 않음 |

주문 조회 결과는 원주문 수량, 누적 체결량, 취소량, 현재 대기 잔량을 함께 제공한다. 접수되지 않은 ID의 조회 결과는 `std::nullopt`다.

## 6. 취소

`cancel(orderId, requestingUserId)`는 대기 중인 잔량 전체를 취소하고 그 수량을 반환한다. 일부 체결 주문은 남은 수량만 취소하며 이미 만들어진 체결은 변경하지 않는다.

먼저 양수가 아닌 주문 ID와 사용자 ID를 각각 `InvalidOrderId`, `InvalidUserId`로 거절한다. 유효한 ID에 대해서는 주문 존재 여부, 요청자 소유 여부, 종료 상태 순서로 검사하므로 없는 주문은 `OrderNotFound`, 타인의 주문은 `NotOwner`, 전량 체결 주문은 `AlreadyFilled`, 이미 취소된 주문은 `AlreadyCanceled`를 반환하며 실패 시 상태는 바뀌지 않는다.

WebSocket에서는 `type: "CANCEL_ORDER"`로 요청하며, 외부 Router가 같은 엔진 Worker에서
이 함수를 호출한다. 통신 필드와 응답은 [백엔드 JSON 통신 명세](backend-json-api.md)를 참고한다.

## 7. 조회와 최근 체결가

`order(orderId)`는 접수된 주문의 현재 상태를 조회한다. `orderBook()`은 가격별 대기 잔량을 합산해 매수는 높은 가격부터, 매도는 낮은 가격부터 반환한다.

`lastTradePrice()`는 실제 체결이 발생할 때만 마지막 체결 가격으로 갱신된다. 최초 체결 전에는 `std::nullopt`이며, 미체결 주문 접수나 주문 취소는 값을 바꾸지 않고 외부 기준가도 사용하지 않는다.

## 8. 공개 인터페이스 사용 예

```cpp
#include "matching_engine/engine.hpp"

matching_engine::Engine engine{1001};

const auto sell = engine.submit({
    .orderId = 2001,
    .userId = 20,
    .type = matching_engine::OrderType::Sell,
    .price = 10'100,
    .quantity = 5,
});

const auto buy = engine.submit({
    .orderId = 1001,
    .userId = 10,
    .type = matching_engine::OrderType::Buy,
    .price = 10'100,
    .quantity = 3,
});

const auto order = engine.order(2001);
const auto book = engine.orderBook();
const auto lastPrice = engine.lastTradePrice();
const auto canceled = engine.cancel(2001, 20);
```

공개 구조체는 직렬화 라이브러리나 DB 드라이버에 의존하지 않는다. 외부 `Router` 계층이
WebSocket 주문 JSON을 `OrderRequest`로 변환하고 결과 구조체를 JSON 응답으로 변환한다.
통신 형식은 [WebSocket 라우터 문서](websocket-router.md)에 정리되어 있다.

## 9. 아직 구현하지 않는 범위

- 자기체결 방지 정책
- 시장가·정정·조건부 주문과 주문 만료
- 영속화, 재시작 복구와 이벤트 로그
- DB 트랜잭션, 계좌 잔고 검증, 자산 예약과 정산
- 코어 내부 네트워크 API와 인증·인가 (외부 `Router`의 WebSocket 신규 주문 접수·취소·현재가 조회는 구현됨)
- 내부 동시성 제어, 다종목 라우팅과 방 스케줄링
- 수수료, 호가 단위표 검증과 가격 제한폭
- 외부 기준가·시세 API 연동

이 기능들은 코어 매칭 규칙과 분리된 상위 계층 또는 후속 명세에서 다룬다.
