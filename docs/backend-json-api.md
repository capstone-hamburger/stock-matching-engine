# 백엔드 ↔ 매칭 엔진 JSON 통신 명세

이 문서는 현재 구현된 WebSocket API 전체를 정리한다. Markdown 파일을 노션으로 가져오거나
본문을 복사해 사용할 수 있도록 표와 JSON 코드 블록으로 작성했다.

## 1. 연결과 공통 규칙

- 기본 주소: `ws://127.0.0.1:9002/`
- 기본 종목: `stockId = 1001`. 실행 설정으로 변경할 수 있으며, 서버 하나는 현재 한 종목을 담당한다.
- 전송: UTF-8 WebSocket **text 메시지**. 메시지 하나에 JSON 객체 하나를 보낸다.
- 요청은 `type`, 응답은 `event` 필드로 종류를 구분한다. 필드명과 값의 대소문자를 구분한다.
- 필드 순서는 자유지만, 각 요청에 명시된 필드는 모두 필요하며 추가 필드는 허용하지 않는다.
- 현재 `payload`, `requestId`, 인증 토큰 필드는 없다. 임의로 추가하면 거절된다.
- 같은 연결의 요청은 순서대로 처리·응답한다. 다른 연결 사이에는 엔진 작업 큐에 들어온 순서가 적용된다.
- 결과는 요청한 연결로만 반환한다. 자동 구독이나 다른 연결로의 방송은 없다.
- 기본 메시지 한도는 16 KiB, 연결 한도는 미완료 핸드셰이크를 포함해 128개다.

### 숫자 규칙

| 종류 | 유효 범위 |
| --- | --- |
| `stockId`, `orderId`, `userId` | 1 ~ 9,223,372,036,854,775,807 (양수 signed int64) |
| 신규 주문의 `price`, `quantity` | 1 ~ 2,147,483,647 (양수 int32 범위) |
| 결과의 수량·가격·체결 ID | JSON 정수; C++ 내부 표현은 int64 |

숫자를 문자열로 보내지 않는다. 소수, 지수 표기로 파싱되는 부동소수점, 불리언, null,
int64 범위 초과 값은 ID·가격·수량으로 허용하지 않는다.
JavaScript에서는 큰 ID를 `Number`로 변환하면 정밀도가 손실될 수 있으므로,
양쪽에서 ID를 안전 정수 범위로 제한하거나 정수를 보존하는 JSON 직렬화·파서를 사용한다.
`price`는 프로젝트가 정한 최소 가격 단위의 정수이며, 예시는 1단위를 1원으로 해석할 수 있다.

## 2. 요청 `type` 값의 의미

| `type` 값 | 의미 | 필수 필드 | 엔진 동작 |
| --- | --- | --- | --- |
| `BUY` | 신규 지정가 매수: 지정가 이하의 매도 주문과 매칭 | `type`, `stockId`, `orderId`, `userId`, `status`, `price`, `quantity` | `Engine::submit()` |
| `SELL` | 신규 지정가 매도: 지정가 이상의 매수 주문과 매칭 | `type`, `stockId`, `orderId`, `userId`, `status`, `price`, `quantity` | `Engine::submit()` |
| `GET_CURRENT_PRICE` | 마지막 실제 체결 가격을 한 번 조회 | `type`, `stockId` | `Engine::lastTradePrice()` |
| `CANCEL_ORDER` | 기존 주문의 미체결 잔량 전체 취소 | `type`, `stockId`, `orderId`, `userId` | `Engine::cancel()` |

현재 `type`은 통신 요청 종류를 나타낸다. `BUY`·`SELL`만 엔진의 주문 방향인
`OrderType`으로 변환된다. `CANCEL_ORDER`·`GET_CURRENT_PRICE`는 새 매수·매도 주문이 아니다.
시장가·중간가·정정·캔들 조회 타입은 아직 지원하지 않는다.

## 3. 응답 `event` 값의 의미

| `event` 값 | 의미 | 성공 여부 확인 |
| --- | --- | --- |
| `ORDER_RESULT` | 신규 BUY/SELL 주문의 엔진 처리 결과 | `accepted` 및 `rejectionReason` |
| `CURRENT_PRICE` | 현재가 조회 결과 | `price`가 정수 또는 최초 체결 전 `null` |
| `CANCEL_RESULT` | 취소 요청의 엔진 처리 결과 | `success` 및 `failureReason` |
| `ERROR` | JSON·필드·종목 오류 또는 내부 처리 예외 | `code`, `message` |

`ORDER_RESULT`와 `CANCEL_RESULT`는 실패 결과도 담을 수 있다.
`event`만 보고 주문 접수나 취소가 성공했다고 판단하지 않는다.

## 4. 신규 매수·매도 주문

### 4.1 매도 요청 예시

```json
{
  "type": "SELL",
  "stockId": 1001,
  "orderId": 2001,
  "userId": 20,
  "status": "PENDING",
  "price": 10100,
  "quantity": 7
}
```

| 필드 | 의미 |
| --- | --- |
| `type` | `BUY`는 매수, `SELL`은 매도 |
| `stockId` | 주문 대상 종목 |
| `orderId` | 백엔드가 발급하는 신규 주문 ID |
| `userId` | 주문 소유자의 ID |
| `status` | 신규 요청은 반드시 `PENDING`; 백엔드가 체결 상태를 지정하지 않음 |
| `price` | 매수 시 최대 지불 가격, 매도 시 최소 판매 가격인 지정가 |
| `quantity` | 원주문 수량 |

빈 엔진에 위 매도만 접수되면 다음과 같이 응답한다.

```json
{
  "event": "ORDER_RESULT",
  "orderId": 2001,
  "stockId": 1001,
  "accepted": true,
  "status": "PENDING",
  "engineStatus": "OPEN",
  "rejectionReason": "NONE",
  "filledQuantity": 0,
  "remainingQuantity": 7,
  "trades": []
}
```

### 4.2 매수 요청과 체결 응답 예시

앞의 매도 주문 이후 아래 매수를 접수한다.

```json
{
  "type": "BUY",
  "stockId": 1001,
  "orderId": 1001,
  "userId": 10,
  "status": "PENDING",
  "price": 10200,
  "quantity": 3
}
```

체결 가격은 새 매수 지정가 10,200이 아니라 기존 매도 주문의 가격 10,100이다.

```json
{
  "event": "ORDER_RESULT",
  "orderId": 1001,
  "stockId": 1001,
  "accepted": true,
  "status": "COMPLETE",
  "engineStatus": "FILLED",
  "rejectionReason": "NONE",
  "filledQuantity": 3,
  "remainingQuantity": 0,
  "trades": [
    {
      "tradeId": 1,
      "buyOrderId": 1001,
      "sellOrderId": 2001,
      "price": 10100,
      "quantity": 3
    }
  ]
}
```

### 4.3 주문 응답 필드와 상태

| 필드 | 의미 |
| --- | --- |
| `orderId`, `stockId` | 이번 요청의 주문·종목 ID |
| `accepted` | 엔진에 주문이 접수됐는지. `true`라도 전량 체결을 의미하지 않음 |
| `status` | ERD 표현: `PENDING`, `COMPLETE`, 거절 시 `null` |
| `engineStatus` | 더 상세한 처리 상태 |
| `rejectionReason` | 접수 성공 시 `NONE`, 거절 시 사유 코드 |
| `filledQuantity` | 이번 신규 주문 처리 중 체결된 수량 |
| `remainingQuantity` | 처리 후 호가창에 남은 수량. 거절이면 0 |
| `trades` | 이번 요청으로 발생한 체결 목록, 발생 순서대로 정렬. 체결이 없으면 빈 배열 |

| 상황 | `accepted` | `status` | `engineStatus` |
| --- | --- | --- | --- |
| 미체결 대기 | `true` | `PENDING` | `OPEN` |
| 일부 체결 후 잔량 대기 | `true` | `PENDING` | `PARTIALLY_FILLED` |
| 전량 체결 | `true` | `COMPLETE` | `FILLED` |
| 신규 주문 거절 | `false` | `null` | `REJECTED` |

`CANCELED`는 엔진 내부의 취소 상태다. 신규 주문 응답에는 나오지 않으며, 취소 API는
별도의 `CANCEL_RESULT`와 `success`로 결과를 알린다.

### 4.4 `Trade` 객체

| 필드 | 의미 |
| --- | --- |
| `tradeId` | 엔진별 체결 순번; 새 엔진에서는 1부터 시작 |
| `buyOrderId` | 체결된 매수 주문 ID |
| `sellOrderId` | 체결된 매도 주문 ID |
| `price` | 실제 체결 가격 |
| `quantity` | 이 체결 한 건의 수량 |

한 주문이 여러 주문과 매칭되면 `trades`에 여러 객체가 들어간다.
종목은 바깥 응답의 `stockId`를 사용한다. 현재 체결 시각(`executedAt`)은 포함하지 않는다.
백엔드는 체결 목록을 이용해 매수·매도 양쪽 주문과 자산을 반영해야 한다.
기존 대기 주문을 보낸 연결에 별도 체결 알림을 보내지는 않는다.

### 4.5 주문 거절

이미 접수된 주문 ID를 다시 보내면 다음과 같이 거절된다.

```json
{
  "event": "ORDER_RESULT",
  "orderId": 1001,
  "stockId": 1001,
  "accepted": false,
  "status": null,
  "engineStatus": "REJECTED",
  "rejectionReason": "DUPLICATE_ORDER_ID",
  "filledQuantity": 0,
  "remainingQuantity": 0,
  "trades": []
}
```

| `rejectionReason` | 의미 |
| --- | --- |
| `NONE` | 접수 성공 |
| `INVALID_ORDER_ID` | 주문 ID가 0 이하 |
| `INVALID_USER_ID` | 사용자 ID가 0 이하 |
| `INVALID_PRICE` | 지정가가 허용 범위를 벗어남 |
| `INVALID_QUANTITY` | 주문 수량이 허용 범위를 벗어남 |
| `DUPLICATE_ORDER_ID` | 엔진 수명 동안 이미 접수된 ID; 체결·취소가 끝난 주문도 포함 |
| `ENGINE_CAPACITY_EXCEEDED` | 체결 ID 또는 호가 수량 합계의 표현 범위 초과 우려 |
| `INVALID_ORDER_TYPE` | 엔진 내부의 방어적 주문 방향 검증. 현재 WebSocket 파서는 잘못된 타입을 먼저 `ERROR`로 거절하므로 정상 API 경로에서는 발생하지 않음 |

## 5. 현재가 조회

### 요청

```json
{
  "type": "GET_CURRENT_PRICE",
  "stockId": 1001
}
```

### 응답

앞의 예시처럼 10,100에서 체결됐다면:

```json
{
  "event": "CURRENT_PRICE",
  "stockId": 1001,
  "price": 10100
}
```

엔진 생성 후 아직 체결이 없다면:

```json
{
  "event": "CURRENT_PRICE",
  "stockId": 1001,
  "price": null
}
```

현재가는 마지막 실제 체결 가격이다. 주문 지정가·최우선 호가·중간가와 다르다.
미체결 주문, 거절, 취소, 조회만으로 가격이 바뀌지 않는다.
한 주문이 여러 가격에서 체결되면 마지막 체결 가격을 반환한다.
과거 가격·캔들·거래량은 이 응답에 포함되지 않는다.

## 6. 주문 취소

### 6.1 요청

```json
{
  "type": "CANCEL_ORDER",
  "stockId": 1001,
  "orderId": 2001,
  "userId": 20
}
```

| 필드 | 의미 |
| --- | --- |
| `type` | 취소 요청임을 나타내는 `CANCEL_ORDER` |
| `stockId` | 취소 대상 주문의 종목 |
| `orderId` | 이미 접수된 취소 대상 주문 ID. 새 ID를 발급하지 않음 |
| `userId` | 취소를 요청하는 사용자 ID. 주문 소유자와 일치해야 함 |

매수·매도 모두 같은 타입으로 취소한다. 방향은 엔진에 저장된 주문에서 확인한다.
남은 수량 **전체**를 취소하며, 취소 수량을 지정하는 기능은 없다.
`quantity`, `price`, `status`를 덧붙이면 `INVALID_REQUEST`로 거절된다.

### 6.2 성공 응답

앞의 예시에서 매도 7주 중 3주가 체결됐으므로 나머지 4주를 취소한다.

```json
{
  "event": "CANCEL_RESULT",
  "orderId": 2001,
  "stockId": 1001,
  "success": true,
  "canceledQuantity": 4,
  "failureReason": "NONE"
}
```

| 필드 | 의미 |
| --- | --- |
| `orderId`, `stockId` | 요청에서 지정한 취소 대상 |
| `success` | 이번 요청으로 취소가 수행됐는지 |
| `canceledQuantity` | 이번 요청으로 취소한 수량. 실패 시 항상 0 |
| `failureReason` | 성공은 `NONE`, 실패는 아래 사유 코드 |

취소 후 해당 주문은 호가 대기열에서 제거되고 엔진 내부 상태는 `CANCELED`가 된다.
이미 발생한 체결과 마지막 체결 가격은 유지하며, 취소 때문에 `Trade`를 새로 만들지 않는다.
응답에는 `status`, `engineStatus`, `remainingQuantity`, `trades` 필드가 없다.
백엔드가 취소 상태와 예약 자금·주식 해제를 별도로 반영해야 하며, ERD의
`PENDING`·`COMPLETE`만으로는 취소 상태를 구분할 수 없으므로 DB 표현을 따로 정해야 한다.

### 6.3 실패 응답과 사유

같은 주문을 다시 취소한 경우:

```json
{
  "event": "CANCEL_RESULT",
  "orderId": 2001,
  "stockId": 1001,
  "success": false,
  "canceledQuantity": 0,
  "failureReason": "ALREADY_CANCELED"
}
```

| `failureReason` | 의미 |
| --- | --- |
| `NONE` | 취소 성공 |
| `INVALID_ORDER_ID` | 주문 ID가 0 이하 |
| `INVALID_USER_ID` | 사용자 ID가 0 이하 |
| `ORDER_NOT_FOUND` | 해당 엔진에 주문이 없음 |
| `NOT_OWNER` | 요청의 `userId`가 주문 소유자와 다름 |
| `ALREADY_FILLED` | 이미 전량 체결되어 취소할 잔량이 없음 |
| `ALREADY_CANCELED` | 이미 취소된 주문 |

실패한 취소는 주문 상태를 변경하지 않는다. 검사 순서는 주문 ID → 사용자 ID →
주문 존재 여부 → 소유자 → 전량 체결 여부 → 취소 여부다.
중복 취소 실패는 이번에 추가 취소한 수량이 0이라는 의미이며, 원래 취소 수량을 재전송하지 않는다.
백엔드에서 예약 자금·주식을 중복으로 해제하지 않도록 처리해야 한다.

### 6.4 실행 순서와 소유자 확인

처리는 `parseRequest()` → `Router::Impl::dispatch()` → 엔진 Worker의
`Engine::cancel()` → `protocol::cancellationResult()` → I/O 스레드의 `Session::reply()` 순서다.
취소도 주문·현재가 조회와 같은 엔진 작업 큐에서 실행한다.

같은 연결에서 주문 다음에 취소를 보내면 주문 처리 이후 취소가 실행된다.
서로 다른 연결에서 취소와 체결 가능한 주문이 동시에 들어오면 큐 접수 순서에 따른다.
체결이 먼저 끝났다면 `ALREADY_FILLED`가 반환될 수 있다.

소유자 검사는 JSON의 `userId`와 저장된 주문의 사용자 ID를 비교하는 것이다.
로그인 인증을 대신하지 않으며, 백엔드가 인증된 사용자의 ID를 넣어야 한다.
원래 주문과 다른 WebSocket 연결에서도 올바른 소유자 ID로 취소할 수 있다.

## 7. 공통 오류 응답

등록되지 않은 종목 요청의 예시:

```json
{
  "event": "ERROR",
  "code": "UNKNOWN_STOCK",
  "message": "No engine registered for stockId"
}
```

취소 요청에 `quantity` 같은 추가 필드를 넣은 경우의 예시:

```json
{
  "event": "ERROR",
  "code": "INVALID_REQUEST",
  "message": "CANCEL_ORDER requires exactly type, stockId, orderId and userId"
}
```

| `code` | 의미 |
| --- | --- |
| `INVALID_JSON` | JSON 구문 오류, 숫자 파싱 오류, 중첩 깊이 제한 초과 |
| `INVALID_ORDER` | 신규 주문 필드·타입 오류 또는 지원하지 않는 요청 타입 |
| `INVALID_REQUEST` | 조회·취소의 필드 누락·추가, ID 표현 오류, 0 이하 `stockId` |
| `UNKNOWN_STOCK` | 유효한 양수 ID지만 서버가 담당하지 않는 종목 |
| `TEXT_REQUIRED` | binary 메시지를 보냄; JSON text 필요 |
| `INTERNAL_ERROR` | 엔진 처리 또는 응답 생성 중 예외 |

취소 요청의 `orderId`·`userId`가 문자열·소수·int64 범위 밖이면 `ERROR/INVALID_REQUEST`다.
int64 정수이지만 0 이하이면 엔진이 `CANCEL_RESULT`의 실패 사유로 반환한다.
조회·취소를 포함한 지원 타입 외의 값은 기존 주문 검증 경로에서 `INVALID_ORDER`로 거절한다.

오류 응답에는 `orderId`, `stockId`, `requestId`가 없다. 백엔드는 연결별 요청 순서로
응답을 대응시키거나, 단순하게 응답을 받은 뒤 다음 요청을 전송할 수 있다.
`message`는 진단용 텍스트이며 처리 분기는 `event`·`code`·사유 코드로 하는 것이 좋다.
형식 오류 후에도 연결을 재사용할 수 있다. 메시지 크기 초과는 JSON 오류 대신
WebSocket close code `1009`로 연결을 닫고, 연결 한도 초과는 upgrade 이전에 TCP 연결을 닫는다.

## 8. 백엔드 연동 시 동작 범위

- 엔진은 잔고·보유수량 검증, 자산 예약, DB 저장·정산을 수행하지 않는다.
- 취소 성공 응답은 엔진 메모리에 취소가 반영됐다는 뜻이며 DB 반영 완료를 뜻하지 않는다.
- 체결·취소된 주문 ID도 같은 엔진 수명 동안 다시 사용할 수 없다. 접수 거절된 주문 ID는 재사용 가능하다.
- 응답 유실이나 연결 종료만으로 주문·취소가 실패했다고 판단할 수 없다. 접수된 작업은 계속 처리될 수 있다.
- 현재 주문 결과 재조회·이벤트 재전송·영속화·재시작 복구는 지원하지 않는다.
- 엔진을 새로 시작하면 주문·호가창·현재가가 초기화되고 체결 ID도 다시 시작한다.
- 실시간 알림 방송, 과거 체결·캔들 조회, 체결 시각 필드는 지원하지 않는다.
- 백엔드와 같은 네트워크 환경에서 실행하면 localhost로 통신할 수 있다. 기본 서버에는 인증·TLS가 없다.

## 9. 빠른 연동 테스트 순서

서버를 새로 시작한 뒤 이 문서의 예시를 순서대로 보내면 된다.

1. `GET_CURRENT_PRICE`: `price: null` 확인.
2. `SELL`: 주문 `2001`, 사용자 `20`, 10,100에 7주 → `OPEN`, 잔량 7.
3. `BUY`: 주문 `1001`, 사용자 `10`, 10,200에 3주 → `FILLED`, 실제 체결 가격 10,100.
4. `GET_CURRENT_PRICE`: `price: 10100` 확인.
5. `CANCEL_ORDER`: 주문 `2001`, 사용자 `20` → 성공, 취소 수량 4.
6. 같은 취소 재전송 → `ALREADY_CANCELED`, 이번 취소 수량 0.
7. `GET_CURRENT_PRICE`: 취소 후에도 `price: 10100` 유지.

자동 검증은 `ctest --preset dev`로 실행한다. 실제 WebSocket 통합 테스트에서
매수·매도 취소, 부분 체결 후 취소, 소유자·상태 오류, 입력 검증, 요청 순서,
취소와 체결의 경합, 현재가 유지와 기존 주문·조회 API를 확인한다.
