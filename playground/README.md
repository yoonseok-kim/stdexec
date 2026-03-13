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

### 기본 (`main.cpp`)

| # | 함수 | 주제 |
|---|---|---|
| 1 | `example_just_and_sync_wait` | `just()` + `sync_wait()` 기본 사용 |
| 2 | `example_then` | `then()` 값 변환, 체이닝, 타입 변환 |
| 3 | `example_let_value` | `let_value()` 비동기 체이닝 (fn이 sender 반환) |
| 4 | `example_when_all` | `when_all()` 병렬 결합, 결과 합산 |
| 5 | `example_thread_pool` | `schedule`, `continues_on`, `starts_on` 스케줄링 |
| 6 | `example_bulk` | `bulk(ex::par, N, fn)` 병렬 반복 |
| 7 | `example_error_handling` | `upon_error`, `upon_stopped`, `let_error`, `stopped_as_optional` |
| 8 | `example_async_scope` | `spawn()` + `on_empty()` 동적 작업 관리 |
| 9 | `example_pipeline` | 실전 패턴: 병렬 센서 읽기 + 결과 합산 |

### Advanced: 코루틴 (`coroutine.cpp`)

| # | 함수 | 주제 |
|---|---|---|
| 1 | `task_basic` | `exec::task<T>` 기본: `co_await`, `co_return` |
| 2 | `pipeline_task` | task 체이닝: task 안에서 다른 task를 `co_await` |
| 3 | `sticky_task` | Scheduler stickiness: `co_await` 후 자동 스케줄러 복귀 |
| 4 | `reschedule_task` | `exec::reschedule_coroutine_on` 명시적 스케줄러 전환 |
| 5 | `cleanup_task` | `exec::at_coroutine_exit` LIFO 정리 작업 등록 |
| 6 | `safe_task` | task 내 에러 처리 (`STDEXEC_TRY` / `STDEXEC_CATCH`) |

### Advanced: 취소 (`cancellation.cpp`)

| # | 함수 | 주제 |
|---|---|---|
| 1 | `example_stop_token_basics` | `inplace_stop_source`, `inplace_stop_token`, stop_callback |
| 2 | `example_sync_wait_stopped` | `sync_wait` 취소 시 `nullopt` 반환, `stopped_as_optional` |
| 3 | `example_when_all_cancellation` | `when_all` 자동 취소 전파 |
| 4 | `example_unless_stop_requested` | `exec::unless_stop_requested`: 사전 취소 시 skip |
| 5 | `example_scope_cancellation` | `async_scope` + 동적 spawn + 완료 대기 |

### Advanced: 커스텀 Sender/Receiver (`custom_sender.cpp`)

| # | 함수 | 주제 |
|---|---|---|
| 1 | `example_minimal_sender` | 최소 커스텀 sender 구현 (`sender_concept`, `connect`, operation state) |
| 2 | `example_multi_channel_sender` | 값/에러 다중 채널 sender |
| 3 | `example_receiver_adaptor` | `exec::receiver_adaptor` CRTP로 커스텀 receiver 작성 |
| 4 | `example_retry_sender` | retry sender 패턴: 에러 시 자동 재시도 알고리즘 |

### Advanced: when_any (`when_any.cpp`)

| # | 함수 | 주제 |
|---|---|---|
| 1 | `example_when_any_basic` | `exec::when_any` 기본: 첫 완료 sender 채택 |
| 2 | `example_timeout_pattern` | `when_any`로 타임아웃 패턴 구현 |
| 3 | `example_redundant_request` | 중복 요청 패턴: 가장 빠른 서버 응답 채택 |
| 4 | `example_when_any_error` | 모든 sender가 에러인 경우의 처리 |

### Advanced: 반복 (`repeat.cpp`)

| # | 함수 | 주제 |
|---|---|---|
| 1 | `example_repeat_n` | `exec::repeat_n(N)` 정확히 N번 반복 |
| 2 | `example_repeat_until` | `exec::repeat_until()` 조건 만족까지 반복 |
| 3 | `example_accumulate_pattern` | `repeat_until`로 결과 누적 패턴 |
| 4 | `example_repeat_with_stop` | `repeat_n` 활용한 유한 반복 패턴 |
| 5 | `example_repeat_on_thread_pool` | thread pool 위에서 `repeat_n` |

### Advanced: 고급 패턴 모음 (`advanced_patterns.cpp`)

| # | 함수 | 주제 |
|---|---|---|
| 1 | `example_split` | `exec::split` multicast: 여러 번 connect 가능한 sender |
| 2 | `example_ensure_started` | `exec::ensure_started` eager 실행 + 결과 캐싱 |
| 3 | `example_finally` | `exec::finally` 결과와 무관한 정리 보장 |
| 4 | `example_create` | `exec::create` 콜백 기반 API를 sender로 래핑 |
| 5 | `example_materialize` | `exec::materialize` / `dematerialize` completion 채널 통일 |

### Advanced: 핸들러 체이닝 (`handler_chaining.cpp`)

| # | 함수 | 주제 |
|---|---|---|
| 1 | `example_basic_chain` | `then` + 예외 기반 핸들러 체인: validate → parse → process → serialize |
| 2 | `example_context_switching_chain` | `continues_on`으로 I/O↔워커 스레드 전환이 있는 핸들러 체인 |
| 3 | `example_middleware_pattern` | Middleware 패턴: logging → auth → core 조합 |
| 4 | `example_parallel_request_handling` | `async_scope`로 다중 요청 병렬 처리 |
| 5 | `example_cancellation_in_chain` | `let_stopped`로 체인 도중 취소 처리 |

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

