/*
 * stdexec Playground - 기본 사용법 테스트
 *
 * stdexec는 C++26 std::execution (P2300) 의 reference implementation입니다.
 * 이 파일은 stdexec의 핵심 개념과 API를 단계별로 시연합니다.
 *
 * 핵심 개념:
 *   - Sender  : 비동기 작업을 기술하는 객체 (lazy, 실행 전까지 아무 일도 안 함)
 *   - Receiver: sender의 결과를 받아 처리하는 콜백 (set_value, set_error, set_stopped)
 *   - Scheduler: 작업이 어디서 실행될지 결정하는 실행 컨텍스트
 *   - Operation State: connect(sender, receiver)로 생성, start()로 실행 개시
 *
 * 빌드 방법:
 *   cmake -S . -B build -GNinja -DCMAKE_BUILD_TYPE=Debug
 *   cmake --build build --target playground
 *   ./build/playground/playground
 */

#include <stdexec/execution.hpp>

#include <exec/async_scope.hpp>
#include <exec/static_thread_pool.hpp>

#include <cstdio>
#include <string>

// stdexec 네임스페이스에 대한 편의 alias
// 관례적으로 ex 또는 stdexec를 사용합니다.
namespace ex = stdexec;

///////////////////////////////////////////////////////////////////////////////
// 헬퍼: 섹션 구분 출력
///////////////////////////////////////////////////////////////////////////////
static void section(char const * title)
{
  std::printf("\n=== %s ===\n", title);
}

///////////////////////////////////////////////////////////////////////////////
// 1. just() + sync_wait() - 가장 기본적인 sender와 consumer
//
// just(values...) : 주어진 값을 즉시 생성하는 sender factory
// sync_wait(sender) : sender를 실행하고 결과를 블로킹으로 기다리는 consumer
//   - 반환 타입: std::optional<std::tuple<Values...>>
//   - 성공 시: optional에 tuple이 담김
//   - 취소 시: nullopt (빈 optional)
//   - 에러 시: exception을 rethrow
///////////////////////////////////////////////////////////////////////////////
static void example_just_and_sync_wait()
{
  section("1. just() + sync_wait()");

  // 정수 값 하나를 생성하는 sender
  ex::sender auto snd = ex::just(42);

  // sync_wait()으로 실행하고 결과를 꺼냄
  // structured binding으로 tuple을 풀어서 사용
  auto [value] = ex::sync_wait(std::move(snd)).value();
  std::printf("just(42) => %d\n", value);

  // 여러 값을 한 번에 생성할 수도 있음
  auto [a, b, c] = ex::sync_wait(ex::just(1, 2.5, std::string{"hello"})).value();
  std::printf("just(1, 2.5, \"hello\") => %d, %.1f, %s\n", a, b, c.c_str());

  // void sender (값 없이 완료)
  // just()에 인자를 주지 않으면 set_value()로 완료 (void completion)
  auto result = ex::sync_wait(ex::just());
  std::printf("just() completed: %s\n", result.has_value() ? "yes" : "no");
}

///////////////////////////////////////////////////////////////////////////////
// 2. then() - 값 변환 (map 연산)
//
// then(fn) : sender가 생성한 값에 함수를 적용하여 변환
// pipe 연산자 `|` 를 사용하여 연결하는 것이 관례
///////////////////////////////////////////////////////////////////////////////
static void example_then()
{
  section("2. then() - 값 변환");

  // 기본: just(5) 의 결과에 *2 적용
  auto [val] = ex::sync_wait(ex::just(5) | ex::then([](int x) { return x * 2; })).value();
  std::printf("just(5) | then(*2) => %d\n", val);

  // 체이닝: 여러 then을 연결
  auto snd = ex::just(10) | ex::then([](int x) { return x + 5; })  // 15
           | ex::then([](int x) { return x * 3; })                 // 45
           | ex::then(
               [](int x)
               {
                 // then 안에서 타입 변환도 가능 (int -> string)
                 return std::string{"result="} + std::to_string(x);
               });
  auto [str] = ex::sync_wait(std::move(snd)).value();
  std::printf("10 -> +5 -> *3 -> string => %s\n", str.c_str());

  // 여러 값을 받는 then
  // just(a, b)가 두 값을 생성하면, then의 람다도 두 파라미터를 받음
  auto [sum] = ex::sync_wait(ex::just(3, 7) | ex::then([](int a, int b) { return a + b; })).value();
  std::printf("just(3, 7) | then(a+b) => %d\n", sum);
}

///////////////////////////////////////////////////////////////////////////////
// 3. let_value() - 비동기 체이닝 (flatmap / bind)
//
// let_value(fn) : fn이 sender를 반환. 이전 sender의 결과를 받아
//                 새로운 비동기 작업을 시작할 수 있음 (async continuation)
//
// 핵심 차이:
//   - then(fn)    : fn은 일반 값을 반환 (동기 변환)
//   - let_value(fn): fn은 sender를 반환 (비동기 체이닝)
//
// 주의: fn의 파라미터는 lvalue reference로 전달됨 (int&, string& 등)
//       이 참조는 반환된 sender가 완료될 때까지 유효함
///////////////////////////////////////////////////////////////////////////////
static void example_let_value()
{
  section("3. let_value() - 비동기 체이닝");

  // 기본: 값을 받아서 새로운 sender를 반환
  auto snd = ex::just(10)
           | ex::let_value(
               [](int& x)
               {
                 // x는 lvalue reference로 전달됨
                 // 새로운 sender를 반환해야 함
                 return ex::just(x * 2 + 1);
               });
  auto [val] = ex::sync_wait(std::move(snd)).value();
  std::printf("just(10) | let_value(x*2+1) => %d\n", val);

  // let_value 체이닝: 단계별 비동기 처리 파이프라인
  auto pipeline = ex::just(std::string{"hello"})
                | ex::let_value(
                    [](std::string& msg)
                    {
                      // 1단계: 대문자 변환 후 새 sender 반환 (비동기 작업 가정)
                      std::string upper;
                      upper.reserve(msg.size());
                      for (auto ch: msg)
                        upper += static_cast<char>(std::toupper(ch));
                      return ex::just(std::move(upper));
                    })
                | ex::let_value(
                    [](std::string& msg)
                    {
                      // 2단계: 접미사 추가
                      return ex::just(msg + " WORLD");
                    });
  auto [result] = ex::sync_wait(std::move(pipeline)).value();
  std::printf("pipeline result => %s\n", result.c_str());
}

///////////////////////////////////////////////////////////////////////////////
// 4. when_all() - 병렬 결합 (join)
//
// when_all(senders...) : 여러 sender를 결합하고 모든 결과를 합침
//                        (scheduler에 따라 병렬 실행 가능)
//   - 모든 sender가 성공하면 값들이 하나의 tuple로 합쳐짐
//   - 하나라도 에러/취소되면 나머지도 취소됨
//   - void sender의 결과는 생략됨
///////////////////////////////////////////////////////////////////////////////
static void example_when_all()
{
  section("4. when_all() - 병렬 결합");

  // 세 개의 sender를 결합
  auto snd = ex::when_all(ex::just(10), ex::just(20), ex::just(30));

  // 결과는 flattened tuple: (int, int, int)
  auto [a, b, c] = ex::sync_wait(std::move(snd)).value();
  std::printf("when_all(10, 20, 30) => %d, %d, %d\n", a, b, c);

  // when_all + then: 결과를 합산
  auto snd2 = ex::when_all(ex::just(2) | ex::then([](int x) { return x * x; }),  // 4
                           ex::just(3) | ex::then([](int x) { return x * x; }),  // 9
                           ex::just(5) | ex::then([](int x) { return x * x; }))  // 25
            | ex::then([](int a, int b, int c) { return a + b + c; });
  auto [total] = ex::sync_wait(std::move(snd2)).value();
  std::printf("sum of squares(2,3,5) => %d\n", total);  // 38
}

///////////////////////////////////////////////////////////////////////////////
// 5. static_thread_pool + schedule() + continues_on() + starts_on()
//
// Scheduler: 작업이 실행될 컨텍스트를 나타냄
// schedule(scheduler) : 해당 스케줄러에서 void 작업을 시작하는 sender
// continues_on(scheduler) : 이후 작업을 지정한 스케줄러로 전환 (pipeable)
// starts_on(scheduler, sender) : sender 시작을 지정한 스케줄러에서 수행 (not pipeable)
///////////////////////////////////////////////////////////////////////////////
static void example_thread_pool()
{
  section("5. static_thread_pool + 스케줄링");

  // 4개 스레드를 가진 thread pool 생성
  exec::static_thread_pool pool{4};
  ex::scheduler auto       sched = pool.get_scheduler();

  // schedule(): pool 위에서 작업 시작
  auto snd1 = ex::schedule(sched)
            | ex::then(
                []
                {
                  std::printf("  schedule: thread %p\n", static_cast<void*>(pthread_self()));
                  return 42;
                });
  auto [v1] = ex::sync_wait(std::move(snd1)).value();
  std::printf("  schedule result: %d\n", v1);

  // continues_on(): 작업을 다른 스케줄러로 전환
  auto snd2 = ex::just(7) | ex::continues_on(sched)  // 이 시점부터 pool thread에서 실행
            | ex::then(
                [](int x)
                {
                  std::printf("  continues_on: thread %p\n", static_cast<void*>(pthread_self()));
                  return x * x;
                });
  auto [v2] = ex::sync_wait(std::move(snd2)).value();
  std::printf("  continues_on result: %d\n", v2);  // 49

  // starts_on(): sender 전체를 특정 스케줄러에서 시작
  // 주의: starts_on은 pipe 연산자를 지원하지 않음 (scheduler가 첫 번째 인자)
  auto snd3 = ex::starts_on(sched,
                            ex::just(100)
                              | ex::then(
                                [](int x)
                                {
                                  std::printf("  starts_on: thread %p\n",
                                              static_cast<void*>(pthread_self()));
                                  return x + 1;
                                }));
  auto [v3] = ex::sync_wait(std::move(snd3)).value();
  std::printf("  starts_on result: %d\n", v3);  // 101

  pool.request_stop();
}

///////////////////////////////////////////////////////////////////////////////
// 6. bulk() - 병렬 반복 실행
//
// bulk(shape, fn) : fn을 [0, shape) 범위로 병렬 반복 실행
// fn의 첫 번째 인자는 인덱스, 나머지는 이전 sender의 결과 (참조)
///////////////////////////////////////////////////////////////////////////////
static void example_bulk()
{
  section("6. bulk() - 병렬 반복");

  exec::static_thread_pool pool{4};
  auto                     sched = pool.get_scheduler();

  // 벡터를 생성하고 bulk로 병렬 초기화
  constexpr int N = 8;

  auto snd = ex::starts_on(sched,
                           ex::just(std::vector<int>(N, 0))
                             | ex::bulk(ex::par,
                                        N,
                                        [](std::size_t idx, std::vector<int>& vec)
                                        {
                                          // 각 인덱스에 대해 병렬로 실행됨
                                          vec[idx] = static_cast<int>(idx * idx);
                                        })
                             | ex::then(
                               [](std::vector<int> vec)
                               {
                                 std::printf("  bulk result: [");
                                 for (std::size_t i = 0; i < vec.size(); ++i)
                                 {
                                   std::printf("%s%d", i > 0 ? ", " : "", vec[i]);
                                 }
                                 std::printf("]\n");
                               }));
  ex::sync_wait(std::move(snd));

  pool.request_stop();
}

///////////////////////////////////////////////////////////////////////////////
// 7. 에러 처리: upon_error(), upon_stopped(), let_error(), let_stopped()
//
// stdexec sender는 세 가지 채널(channel)로 완료될 수 있음:
//   - set_value(values...)  : 성공
//   - set_error(error)      : 에러
//   - set_stopped()         : 취소
//
// upon_error(fn)   : 에러를 동기적으로 변환 (에러 -> 값)
// upon_stopped(fn) : 취소를 동기적으로 변환 (취소 -> 값)
// let_error(fn)    : 에러를 비동기적으로 처리 (fn이 sender 반환)
// let_stopped(fn)  : 취소를 비동기적으로 처리 (fn이 sender 반환)
///////////////////////////////////////////////////////////////////////////////
static void example_error_handling()
{
  section("7. 에러 처리");

  // upon_error: 에러를 값으로 변환
  // 에러 타입은 exception_ptr뿐 아니라 임의 타입도 가능
  auto snd1 = ex::just_error(std::string{"something went wrong"})
            | ex::upon_error(
                [](std::string err)
                {
                  std::printf("  caught error: %s\n", err.c_str());
                  return -1;  // 에러를 값(-1)으로 변환
                });
  auto [v1] = ex::sync_wait(std::move(snd1)).value();
  std::printf("  upon_error result: %d\n", v1);  // -1

  // upon_stopped: 취소를 값으로 변환
  auto snd2 = ex::just_stopped()
            | ex::upon_stopped(
                []
                {
                  std::printf("  operation was cancelled\n");
                  return 0;  // 취소를 값(0)으로 변환
                });
  auto [v2] = ex::sync_wait(std::move(snd2)).value();
  std::printf("  upon_stopped result: %d\n", v2);  // 0

  // let_error: 에러를 비동기적으로 복구 (sender 반환)
  auto snd3 = ex::just_error(std::string{"fail"})
            | ex::let_error(
                [](std::string& err)
                {
                  // 에러를 복구하고 새로운 비동기 작업으로 대체
                  std::printf("  recovering from: %s\n", err.c_str());
                  return ex::just(999);
                });
  auto [v3] = ex::sync_wait(std::move(snd3)).value();
  std::printf("  let_error recovery: %d\n", v3);  // 999

  // stopped_as_optional: 취소를 optional로 변환
  // 성공 시: optional{value}, 취소 시: nullopt
  auto snd4  = ex::just(42) | ex::stopped_as_optional();
  auto [opt] = ex::sync_wait(std::move(snd4)).value();
  std::printf("  stopped_as_optional: %d\n", opt.value());  // 42
}

///////////////////////////////////////////////////////////////////////////////
// 8. async_scope - 동적 비동기 작업 관리
//
// async_scope는 fire-and-forget 작업을 안전하게 관리하는 도구
//   - spawn(): sender를 실행하고 잊음 (결과 불필요)
//   - on_empty(): 모든 spawn된 작업이 완료될 때까지 대기하는 sender
///////////////////////////////////////////////////////////////////////////////
static void example_async_scope()
{
  section("8. async_scope - 동적 작업 관리");

  exec::static_thread_pool pool{4};
  auto                     sched = pool.get_scheduler();
  exec::async_scope        scope;

  // 여러 작업을 spawn (fire-and-forget)
  for (int i = 0; i < 5; ++i)
  {
    scope.spawn(ex::starts_on(sched,
                              ex::just(i)
                                | ex::then(
                                  [](int idx)
                                  {
                                    std::printf("  spawned task %d on thread %p\n",
                                                idx,
                                                static_cast<void*>(pthread_self()));
                                  })));
  }

  // 모든 spawn된 작업이 완료될 때까지 대기
  ex::sync_wait(scope.on_empty());
  std::printf("  all spawned tasks completed\n");

  pool.request_stop();
}

///////////////////////////////////////////////////////////////////////////////
// 9. 실전 패턴: 비동기 파이프라인 조합
//
// stdexec의 강점은 sender를 조합하여 복잡한 비동기 워크플로우를 구성하는 것
// 아래는 "여러 데이터를 병렬 처리 -> 결과 합산" 패턴의 예시
///////////////////////////////////////////////////////////////////////////////
static void example_pipeline()
{
  section("9. 실전 패턴: 비동기 파이프라인");

  exec::static_thread_pool pool{4};
  auto                     sched = pool.get_scheduler();

  // 시나리오: 세 개의 "센서"에서 병렬로 데이터를 읽고, 결과를 합산
  auto read_sensor = [&](int sensor_id, double base_value)
  {
    return ex::starts_on(sched,
                         ex::just(sensor_id, base_value)
                           | ex::then(
                             [](int id, double val)
                             {
                               // 센서 읽기 시뮬레이션
                               double reading = val + static_cast<double>(id) * 0.1;
                               std::printf("  sensor %d: reading = %.2f\n", id, reading);
                               return reading;
                             }));
  };

  // 세 센서를 병렬로 읽고 결과의 평균을 계산
  auto pipeline = ex::when_all(read_sensor(1, 10.0), read_sensor(2, 20.0), read_sensor(3, 30.0))
                | ex::then(
                    [](double a, double b, double c)
                    {
                      double avg = (a + b + c) / 3.0;
                      std::printf("  average reading: %.2f\n", avg);
                      return avg;
                    });

  auto [avg] = ex::sync_wait(std::move(pipeline)).value();
  std::printf("  final result: %.2f\n", avg);

  pool.request_stop();
}

///////////////////////////////////////////////////////////////////////////////
// main
///////////////////////////////////////////////////////////////////////////////
auto main() -> int
{
  std::printf("stdexec Playground\n");
  std::printf("==================\n");

  example_just_and_sync_wait();  // 1. 기본: just + sync_wait
  example_then();                // 2. 값 변환: then
  example_let_value();           // 3. 비동기 체이닝: let_value
  example_when_all();            // 4. 병렬 결합: when_all
  example_thread_pool();         // 5. 스레드풀 + 스케줄링
  example_bulk();                // 6. 병렬 반복: bulk
  example_error_handling();      // 7. 에러 처리
  example_async_scope();         // 8. 동적 작업 관리: async_scope
  example_pipeline();            // 9. 실전 파이프라인 패턴

  std::printf("\n==================\n");
  std::printf("All examples completed!\n");
  return 0;
}
