# 현재가 조회 기능

## 현재가의 의미

현재가는 엔진에서 마지막으로 실제 체결된 가격이다. 주문 지정가나 최우선 호가가 아니다.
체결이 없는 주문 접수·거절·취소·조회는 현재가를 바꾸지 않는다. 최초 체결 전에는 null이다.
가격 단위는 기존 주문·체결의 price와 동일하다.

## 요청

기존 WebSocket 주소(기본 ws://127.0.0.1:9002/)로 JSON text 메시지를 보낸다.

~~~json
{
  "type": "GET_CURRENT_PRICE",
  "stockId": 1001
}
~~~

- type: 대소문자를 구분하는 GET_CURRENT_PRICE.
- stockId: 양수 signed int64 범위의 JSON 정수이며 서버에 등록된 종목이어야 한다.
- 두 필드만 허용한다. orderId, userId, status, quantity, price는 필요 없다.
- 기존 일곱 필드의 BUY·SELL 주문 형식과 ORDER_RESULT 응답은 그대로 유지한다.
- 이 타입은 통신 요청의 종류다. 엔진의 매수·매도 enum인 OrderType이나 DB 주문 종류에 추가하지 않는다.

## 응답

체결이 있는 경우:

~~~json
{
  "event": "CURRENT_PRICE",
  "stockId": 1001,
  "price": 10100
}
~~~

최초 체결 전:

~~~json
{
  "event": "CURRENT_PRICE",
  "stockId": 1001,
  "price": null
}
~~~

체결이 없다고 0원으로 표시하지 않는다. 요청한 연결로 한 번 응답하며 자동 구독이나 방송은 아니다.
한 주문에서 여러 체결이 발생했다면 그 주문의 마지막 체결 가격을 반환한다.

## 처리 흐름과 함수

~~~text
백엔드 → WebSocket 조회 요청
  → protocol::parseRequest(): 요청 타입·필드 검증
  → Router::Impl::dispatch(): 등록 종목 확인
  → 엔진 Worker 큐에서 Engine::lastTradePrice() 호출
  → protocol::currentPriceResult(): 응답 JSON 생성
  → 요청한 연결로 응답
~~~

기존 엔진 함수 std::optional<Price> Engine::lastTradePrice() const noexcept를 재사용한다.
새 응답 함수는 protocol::currentPriceResult(StockId, std::optional<Price>)다.

조회도 주문과 같은 엔진 Worker에서 순차 실행한다. I/O 스레드가 엔진을 직접 읽지 않으므로
주문 처리와 조회 사이에 데이터 경쟁이 발생하지 않는다. 같은 연결에서 주문 다음에 조회하면
해당 주문 처리 이후의 현재가를 받는다. 다른 연결의 요청은 Worker 큐에 들어온 순서로 처리된다.

## 오류

| 코드 | 의미 |
| --- | --- |
| INVALID_REQUEST | 현재가 조회 필드 누락·추가, 잘못된 stockId 타입·범위 |
| UNKNOWN_STOCK | 서버에 등록되지 않은 종목 |
| INVALID_JSON | JSON 구문 오류 또는 중첩 제한 초과 |
| TEXT_REQUIRED | JSON text가 아닌 binary 메시지 |
| INTERNAL_ERROR | 조회 처리 또는 응답 생성 실패 |

오류 응답은 기존 ERROR event와 code, message 필드를 사용한다.
GET_CURRENT_PRICE가 아닌 지원되지 않는 타입은 기존 주문 검증 경로에서 INVALID_ORDER로 거절된다.

## 차트 기능과의 관계

이 기능은 조회 시점의 최근 체결가만 반환한다. 체결 이력, 시각, 거래량, 캔들은 반환하지 않는다.
현재가를 주기적으로 조회하는 것만으로 정확한 캔들 고가·저가·거래량을 복원할 수 없다.
캔들은 모든 체결 원본을 백엔드가 저장·집계하는 별도 기능으로 구현한다.
엔진은 메모리 상태만 유지하므로 서버를 새로 시작하면 최초 체결 전 상태로 돌아간다.

## 검증

ctest --preset dev의 WebSocket 통합 테스트에서 다음을 확인한다.

- 최초 체결 전 null, 미체결 주문 후 null
- 여러 가격의 연속 체결 후 마지막 실제 체결 가격
- 주문 직후 조회 순서와 여러 연결에서의 동일한 가격
- 미체결 주문·거절·반복 조회 이후 가격 및 엔진 상태 유지
- 잘못된 종목·필드·타입·정수 범위 거절과 이후 연결 재사용
- 기본값이 아닌 종목 설정과 새 엔진의 초기 상태
