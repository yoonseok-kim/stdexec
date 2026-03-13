# stdexec Playground

stdexec (P2300) 라이브러리의 핵심 API를 단계별로 시연하는 playground입니다.

## 빌드 및 실행

```bash
# 프로젝트 루트에서
cmake -S . -B build -GNinja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target playground
./build/playground/playground
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

## 예제 목록 (`main.cpp`)

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

## 주의사항

- sender는 기본적으로 **move-only**입니다. `std::move(snd)`로 전달하세요.
- `let_value`의 람다 파라미터는 **lvalue reference** (`int&`)로 전달됩니다.
- `starts_on(scheduler, sender)`는 pipe `|`를 지원하지 않습니다 (scheduler가 첫 번째 인자).
- `continues_on(scheduler)`는 pipe를 지원합니다.
- `sync_wait`은 성공 시 `optional<tuple<...>>`을, 취소 시 `nullopt`을, 에러 시 exception rethrow를 합니다.
