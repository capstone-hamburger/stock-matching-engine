# Mock Stock Matching Engine

C++20으로 작성하는 모의 주식 거래 매칭 엔진 MVP의 시작 프로젝트입니다.

엔진 인스턴스 하나가 한 종목의 지정가 호가창을 관리하며, 가격-시간 우선 매칭,
부분·연속 체결, 주문 취소, 주문·호가·최근 체결가 조회를 지원합니다. 상세한 규칙과
공개 인터페이스 사용법은 [`docs/matching-engine-spec.md`](docs/matching-engine-spec.md)에
정리되어 있습니다.

엔진의 동기 실행 구조, 내부 함수 호출 관계, 자료구조와 멀티스레드 확장 방향은
[`docs/matching-engine-architecture.md`](docs/matching-engine-architecture.md)에 정리되어 있습니다.

WebSocket 라우터는 별도 I/O 스레드에서 ERD `Order` JSON을 받고, 엔진 Worker
스레드에서 기존 `Engine::submit()`을 실행합니다. 요청·응답 형식과 스레드 경계는
[`docs/websocket-router.md`](docs/websocket-router.md)를 참고하세요.
현재가 조회는 `type: "GET_CURRENT_PRICE"` 요청으로 제공하며,
사용법은 [현재가 조회 기능](docs/current-price.md)을 참고하세요.
초기화부터 종료까지 함수별 호출 순서와 컨텍스트 이동은
[`docs/websocket-router-flow.md`](docs/websocket-router-flow.md)에 설명되어 있습니다.

## 요구 환경

- WSL2 Linux
- C++20을 지원하는 컴파일러(GCC 10+ 또는 Clang 10+)
- CMake 3.20+
- Make
- GDB
- Git
- Boost 1.74+ (Beast, Asio, System)
- nlohmann/json 3.10+

Ubuntu/WSL에서 도구가 없다면 다음 명령으로 설치할 수 있습니다.

```bash
sudo apt update
sudo apt install -y build-essential cmake gdb git libboost-dev libboost-system-dev nlohmann-json3-dev
```

Ninja를 사용하고 싶다면 `ninja-build`를 추가로 설치하고 CMake 프리셋의 generator를
`Ninja`로 바꾸면 됩니다.

## 빌드 및 실행

```bash
cmake --preset dev
cmake --build --preset dev
./build/dev/app/matching_engine_server
```

기본 접속 주소는 `ws://127.0.0.1:9002/`, 종목은 `stockId=1001`입니다.
`Ctrl+C` 또는 `SIGTERM`으로 종료합니다. 주소·포트·종목을 바꾸려면:

```bash
./build/dev/app/matching_engine_server 0.0.0.0 9002 1001
```

기존 로컬 매칭 데모도 `./build/dev/app/matching_engine_cli`로 실행할 수 있습니다.

개발 패키지를 시스템에 설치하지 않고 `build/deps/usr`에 추출한 환경에서는
최초 설정 시 아래 경로를 지정합니다. 이후 `dev` 빌드는 CMake 캐시를 사용합니다.

```bash
cmake --preset dev -DCMAKE_PREFIX_PATH="$PWD/build/deps/usr"
```

## 테스트

```bash
ctest --preset dev
```

엔진 단위 테스트와 실제 로컬 WebSocket 통신 통합 테스트를 실행합니다.
통합 테스트는 OS가 선택한 임시 포트를 사용합니다.

## 디렉터리 구조

```text
app/        실행 파일
cmake/      공통 CMake 설정
docs/       동작 명세와 공개 API 사용 예
engine/     매칭 엔진 라이브러리와 공개 헤더
router/     WebSocket I/O, 주문 JSON 변환, 엔진 Worker 연결
tests/      테스트 실행 파일
```

DB, 잔고 검증과 계좌 정산은 구현하지 않았습니다. WebSocket 라우터는 현재 신규
BUY/SELL 주문 접수와 현재가 조회를 제공하며 인증·TLS, 취소·기타 조회 API와 체결 알림 방송은 후속 범위입니다.
