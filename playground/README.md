# stdexec Playground

stdexec (P2300) 라이브러리의 핵심 API를 단계별로 시연하는 playground입니다.

## 빌드 및 실행

```bash
# 프로젝트 루트에서
cmake -S . -B build -GNinja -DCMAKE_BUILD_TYPE=Debug

# 기본 예제
cmake --build build --target playground
./build/playground/playground

# 개별 advanced 예제
cmake --build build --target playground.coroutine
cmake --build build --target playground.cancellation
cmake --build build --target playground.custom_sender
cmake --build build --target playground.when_any
cmake --build build --target playground.repeat
cmake --build build --target playground.advanced_patterns
```

## 핵심 개념

stdexec는 **structured concurrency** 모델 기반의 비동기 실행 프레임워크입니다.

| 개념 | 설명 |
|---|---|
| **Sender** | 비동기 작업을 기술하는 객체. lazy하게 동작하여 실행 전까지 아무 일도 하지 않음 |
| **Receiver** | sender의 결과를 받아 처리하는 콜백 (`set_value`, `set_error`, `set_stopped`) |
| **Scheduler** | 작업이 어디서 실행될지 결정하는 실행 컨텍스트 |
| **Operation State** | `connect(sender, receiver)`로 생성하고 `start()`로 실행을 개시하는 객체 |

sender는 세 가지 채널로 완료될 수 있습니다:
- `set_value(values...)` -- 성공
- `set_error(error)` -- 에러
- `set_stopped()` -- 취소

## API 요약

### Sender Factories (sender 생성)

```cpp
namespace ex = stdexec;

ex::just(42)              // 값 42를 즉시 생성하는 sender
ex::just(1, 2.5, "hi")   // 여러 값을 한 번에 생성
ex::just()                // void completion (값 없이 성공)
ex::just_error(err)       // 에러를 즉시 생성하는 sender
ex::just_stopped()        // 취소 신호를 즉시 생성하는 sender
ex::schedule(scheduler)   // 특정 스케줄러에서 void 작업을 시작
```

### Sender Adaptors (sender 변환)

pipe 연산자 `|` 로 연결하는 것이 관례입니다.

```cpp
// then: 동기적 값 변환 (map)
ex::just(5) | ex::then([](int x) { return x * 2; })    // => 10

// let_value: 비동기 체이닝 (flatmap). fn이 sender를 반환
// 주의: 파라미터는 lvalue reference (int&)로 전달됨
ex::just(10) | ex::let_value([](int& x) { return ex::just(x + 1); })

// when_all: 여러 sender를 동시에 실행하고 결과를 합침
ex::when_all(ex::just(1), ex::just(2), ex::just(3))
  | ex::then([](int a, int b, int c) { return a + b + c; })

// continues_on: 이후 작업을 다른 스케줄러로 전환 (pipeable)
ex::just(7) | ex::continues_on(pool_scheduler) | ex::then(fn)

// starts_on: sender를 특정 스케줄러에서 시작 (not pipeable)
ex::starts_on(scheduler, ex::just(100) | ex::then(fn))

// bulk: 병렬 반복 실행. fn(index, values&...)
ex::just(vec) | ex::bulk(ex::par, count, [](size_t i, auto& v) { ... })
```

### 에러 처리

```cpp
// upon_error: 에러를 동기적으로 값으로 변환
sender | ex::upon_error([](auto err) { return default_value; })

// upon_stopped: 취소를 동기적으로 값으로 변환
sender | ex::upon_stopped([] { return fallback_value; })

// let_error: 에러를 비동기적으로 복구 (fn이 sender 반환)
sender | ex::let_error([](auto& err) { return ex::just(recovery); })

// stopped_as_optional: 성공이면 optional{value}, 취소이면 nullopt
sender | ex::stopped_as_optional()
```

### Consumer (sender 실행)

```cpp
// sync_wait: 블로킹으로 결과 대기. 반환: optional<tuple<Values...>>
auto [val] = ex::sync_wait(sender).value();
```

### 실행 컨텍스트

```cpp
#include <exec/static_thread_pool.hpp>
#include <exec/async_scope.hpp>

// 스레드풀
exec::static_thread_pool pool{4};
auto sched = pool.get_scheduler();
// ... 사용 후
pool.request_stop();

// async_scope: fire-and-forget 작업 관리
exec::async_scope scope;
scope.spawn(ex::starts_on(sched, some_sender));   // 실행 후 잊음
ex::sync_wait(scope.on_empty());                   // 모든 작업 완료 대기
```

## 예제 파일 목록

### 기본 (`main.cpp`) — stdexec 입문

stdexec의 핵심 API를 순서대로 학습합니다. sender factory → adaptor → consumer → scheduler → 에러 처리 순서로 진행합니다.

| # | 함수 | 목적 |
|---|---|---|
| 1 | `example_just_and_sync_wait` | **sender/consumer 기본**. `just()`로 값을 생성하고 `sync_wait()`로 결과를 꺼내는 가장 기초적인 패턴. 단일 값, 다중 값, void sender를 모두 시연 |
| 2 | `example_then` | **동기 변환 (map)**. pipe `\|` 연산자로 `then()`을 연결하여 값을 변환. 체이닝, 타입 변환(int→string), 다중 인자 처리를 포함 |
| 3 | `example_let_value` | **비동기 체이닝 (flatmap)**. `then`과 달리 fn이 sender를 반환하는 `let_value`의 핵심 차이를 시연. 파라미터가 lvalue reference인 점에 주의 |
| 4 | `example_when_all` | **여러 sender 결합**. `when_all`로 sender들을 결합하고 결과를 flattened tuple로 받는 패턴. 스케줄러 없이 사용 시 병렬 실행이 아닌 결합임을 학습 |
| 5 | `example_thread_pool` | **스케줄러 3종 비교**. `schedule()`(스케줄러에서 시작), `continues_on()`(중간에 전환, pipeable), `starts_on()`(전체를 특정 스케줄러에서 시작, not pipeable)의 차이를 실습 |
| 6 | `example_bulk` | **병렬 반복**. `bulk(ex::par, N, fn)`으로 벡터를 병렬 초기화하는 패턴. execution policy(`ex::par`) 사용법 포함 |
| 7 | `example_error_handling` | **3채널 에러 모델**. `upon_error`(동기 복구), `upon_stopped`(취소→값), `let_error`(비동기 복구), `stopped_as_optional`(취소→optional) 4가지 패턴 비교 |
| 8 | `example_async_scope` | **fire-and-forget**. `async_scope::spawn()`으로 여러 작업을 실행하고 `on_empty()`로 모든 완료를 대기. spawn은 void sender만 받는다는 제약 학습 |
| 9 | `example_pipeline` | **실전 조합**. thread pool + `when_all` + `starts_on` + `then`을 조합하여 "3개 센서 병렬 읽기 → 평균 계산" 파이프라인 구성 |

### Advanced: 코루틴 (`coroutine.cpp`) — exec::task와 스케줄러 친화성

C++20 코루틴과 stdexec의 통합을 학습합니다. `exec::task<T>`가 sender이기도 하다는 점이 핵심입니다.

| # | 함수 | 목적 |
|---|---|---|
| 1 | `task_basic` | **task 기본**. `exec::task<T>` 안에서 sender를 `co_await`하고 `co_return`으로 값을 반환. task 자체가 sender이므로 `sync_wait`으로 실행 가능 |
| 2 | `pipeline_task` | **task 체이닝**. task가 다른 task를 `co_await`하여 비동기 함수 호출처럼 사용하는 패턴 (`fetch_data → process_data`) |
| 3 | `sticky_task` | **스케줄러 친화성**. `co_await`으로 다른 스케줄러의 작업을 기다려도, 완료 후 자동으로 원래 스케줄러로 복귀함을 thread ID로 검증 |
| 4 | `reschedule_task` | **명시적 스케줄러 전환**. `exec::reschedule_coroutine_on(sched)`으로 stickiness 자체를 변경. 이후 작업은 새 스케줄러에서 계속 실행됨 |
| 5 | `cleanup_task` | **코루틴 종료 정리**. `exec::at_coroutine_exit`으로 LIFO 순서의 정리 작업 등록. co_return 이후에 실행되므로 반환값에는 영향 없음 |
| 6 | `safe_task` | **task 에러 처리**. 코루틴 내부에서 예외 발생 시 `STDEXEC_TRY/CATCH`로 복구하는 패턴 |

### Advanced: 취소 (`cancellation.cpp`) — stop token과 취소 전파

stdexec의 structured cancellation 모델을 학습합니다. stop_source → stop_token → stop_callback의 흐름이 핵심입니다.

| # | 함수 | 목적 |
|---|---|---|
| 1 | `example_stop_token_basics` | **stop token 기본**. `inplace_stop_source`로 stop 요청, `inplace_stop_token`으로 상태 확인, `inplace_stop_callback`으로 콜백 등록하는 3단계 학습 |
| 2 | `example_stopped_handling` | **stopped 채널 처리**. `upon_stopped`로 stopped를 값으로 변환, `stopped_as_optional`로 optional<T>로 변환하는 두 가지 패턴 |
| 3 | `example_when_all_cancellation` | **when_all 자동 취소**. 한 자식이 에러로 완료되면 나머지에 stop이 전파됨을 `let_error`로 관찰 |
| 4 | `example_unless_stop_requested` | **사전 취소 확인**. `exec::unless_stop_requested`로 이미 stop 요청된 환경에서 작업을 건너뛰는 패턴. `write_env` + `prop(get_stop_token, ...)` 활용 |
| 5 | `example_scope_fan_out` | **async_scope fan-out/fan-in**. 여러 작업을 spawn하고 `on_empty()`로 모든 완료를 대기하는 패턴 |

### Advanced: 커스텀 Sender/Receiver (`custom_sender.cpp`) — 확장 포인트

stdexec의 sender/receiver 프로토콜을 직접 구현하는 방법을 단계별로 학습합니다.

| # | 함수 | 목적 |
|---|---|---|
| 1 | `example_minimal_sender` | **최소 sender**. `sender_concept` 태그, `completion_signatures`, `connect()` + operation state `start()`의 4가지 필수 요소로 커스텀 sender 구현 |
| 2 | `example_multi_channel_sender` | **다중 채널 sender**. `set_value`와 `set_error` 두 채널을 모두 갖는 sender. 외부 카운터로 성공/실패를 제어하여 `upon_error`로 복구하는 패턴 |
| 3 | `example_receiver_adaptor` | **receiver 래핑**. `exec::receiver_adaptor<Derived>` CRTP로 `set_value`만 오버라이드하고 나머지는 base()로 자동 forwarding하는 패턴 |
| 4 | `example_retry_sender` | **retry 알고리즘 구현**. operation state 내부에서 `optional`로 중첩 operation을 재구성하여 에러 시 자동 재시도하는 고급 패턴. `examples/algorithms/retry.hpp` 참조 |

### Advanced: when_any (`when_any.cpp`) — 경쟁 실행

여러 sender 중 가장 먼저 완료되는 것의 결과를 채택하고 나머지를 취소하는 패턴을 학습합니다.

| # | 함수 | 목적 |
|---|---|---|
| 1 | `example_when_any_basic` | **기본 경쟁**. `exec::when_any`로 빠른 sender와 느린 sender를 경쟁시키고 첫 완료 결과를 채택. 나머지는 자동 취소됨 |
| 2 | `example_timeout_pattern` | **타임아웃 구현**. 실제 작업 sender와 타임아웃 sender를 `when_any`로 경쟁시켜 timeout 패턴 구현. `std::variant`로 결과 구분 |
| 3 | `example_redundant_request` | **중복 요청**. 같은 요청을 여러 서버에 동시 전송하고 가장 빠른 응답만 사용하는 tail latency 최적화 패턴 |
| 4 | `example_when_any_error` | **전원 실패 처리**. 모든 sender가 에러로 완료될 때 `when_any`가 에러를 전파하고 `let_error`로 복구하는 패턴 |

### Advanced: 반복 (`repeat.cpp`) — 비동기 루프

sender를 반복 실행하는 3가지 어댑터를 학습합니다. 내부적으로 trampoline scheduler로 stack overflow를 방지합니다.

| # | 함수 | 목적 |
|---|---|---|
| 1 | `example_repeat_n` | **고정 횟수 반복**. void sender를 정확히 N번 반복. 가장 기본적인 비동기 루프 |
| 2 | `example_repeat_until` | **조건부 반복**. bool을 반환하는 sender가 true를 반환할 때까지 반복. 폴링/대기 패턴에 유용 |
| 3 | `example_accumulate_pattern` | **누적 패턴**. `repeat_until` + 외부 mutable 변수로 루프 결과를 누적. `sync_wait`가 블로킹하므로 참조 캡처가 안전함을 학습 |
| 4 | `example_repeat_n_with_sideeffect` | **반복 + 사이드 이펙트**. `repeat_n`으로 유한 반복하며 각 iteration의 사이드 이펙트를 관찰 |
| 5 | `example_repeat_on_thread_pool` | **스레드풀 + 반복**. `starts_on(sched, ...)` 위에서 `repeat_n` 실행. `std::atomic`으로 thread-safe한 카운터 관리 |

### Advanced: 고급 패턴 모음 (`advanced_patterns.cpp`) — 유틸리티 API

`exec::` 네임스페이스의 확장 유틸리티를 개별적으로 학습합니다.

| # | 함수 | 목적 |
|---|---|---|
| 1 | `example_split` | **multicast**. `exec::split()`으로 sender를 copyable하게 만들어 여러 번 connect/start 가능. 원본 작업은 한 번만 실행됨을 카운터로 검증 |
| 2 | `example_ensure_started` | **eager 실행**. `exec::ensure_started()`로 sender를 즉시 시작. `split`은 lazy(첫 start 때 실행)이지만 `ensure_started`는 호출 즉시 실행 개시 |
| 3 | `example_finally` | **정리 보장**. `exec::finally(work, cleanup)`으로 성공/에러/취소 무관하게 cleanup 실행을 보장. pipe 문법 `work \| exec::finally(cleanup)`도 시연 |
| 4 | `example_create` | **콜백 API 브릿지**. `exec::create<Sigs>(fn, args...)`로 레거시 콜백 기반 API를 sender로 래핑. `ctx.receiver`에 `set_value`를 호출하여 완료 |
| 5 | `example_materialize` | **completion 채널 통일**. `exec::materialize`로 모든 completion(value/error/stopped)을 값 채널로 변환. generic 알고리즘 작성에 유용. `dematerialize`로 역변환 |

### Advanced: 핸들러 체이닝 (`handler_chaining.cpp`) — 실전 서버 패턴

HTTP 서버 스타일의 비동기 요청 처리 파이프라인을 구성하는 방법을 학습합니다.

| # | 함수 | 목적 |
|---|---|---|
| 1 | `example_basic_chain` | **핸들러 체인**. `then(validate) \| then(parse) \| then(process) \| then(serialize) \| upon_error(...)` 패턴. 각 핸들러는 일반 함수이고, 에러는 예외→`exception_ptr`로 통일 |
| 2 | `example_context_switching_chain` | **스레드 전환**. `starts_on(io) → continues_on(worker) → continues_on(io)`로 I/O 스레드와 워커 스레드를 오가는 실제 서버 패턴 |
| 3 | `example_middleware_pattern` | **middleware 조합**. `with_logging(with_auth(core_handler))` 형태의 함수 합성. 각 middleware는 핸들러를 감싸는 래퍼 함수 |
| 4 | `example_parallel_request_handling` | **병렬 요청 처리**. `async_scope::spawn`으로 6개 요청을 thread pool에서 동시 처리하고 성공/실패 카운트 |
| 5 | `example_cancellation_in_chain` | **체인 취소**. `let_stopped`를 체인 끝에 추가하여 어느 단계에서든 취소 시 503 응답 생성. 정상/에러/취소 3가지 케이스 시연 |

## 주의사항

- sender는 기본적으로 **move-only**입니다. `std::move(snd)`로 전달하세요.
- `let_value`의 람다 파라미터는 **lvalue reference** (`int&`)로 전달됩니다.
- `starts_on(scheduler, sender)`는 pipe `|`를 지원하지 않습니다 (scheduler가 첫 번째 인자).
- `continues_on(scheduler)`는 pipe를 지원합니다.
- `sync_wait`은 성공 시 `optional<tuple<...>>`을, 취소 시 `nullopt`을, 에러 시 exception rethrow를 합니다.
- `stopped_as_optional`은 **값 채널이 있는 sender**에만 적용 가능합니다. (`just_stopped()`에는 불가)
- `upon_error` 람다의 인자 타입은 sender의 에러 타입과 정확히 일치해야 합니다. 여러 에러 타입이 있으면 `let_error([](auto& e) {...})`를 사용하세요.
- `exec::task<T>`는 반드시 scheduler가 연관된 환경에서 `co_await`해야 합니다.
- **핸들러 체이닝에서 에러 타입 통일**: `let_error` 람다는 단일 에러 타입만 받을 수 있습니다. 여러 단계에서 에러가 발생할 때 `exception_ptr`로 통일하려면, 각 핸들러를 `then()` 내부에서 실행하고 에러 시 예외를 throw하세요. `upon_error([](std::exception_ptr ep){...})`로 통일된 처리를 할 수 있습니다.
- **middleware 패턴**: middleware 내부에서 조건 분기로 서로 다른 sender 타입을 반환하면 컴파일 에러가 발생합니다. 동일한 타입을 반환하거나, 핸들러를 값을 반환하는 일반 함수로 작성하고 예외로 에러를 전달하세요.

