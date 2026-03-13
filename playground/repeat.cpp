/*
 * stdexec Playground - repeat 패턴 (Advanced)
 *
 * exec::repeat / repeat_until / repeat_n 은 sender를 반복 실행하는 어댑터입니다.
 * 폴링, 재시도, 루프 등 반복 패턴을 비동기적으로 표현할 수 있습니다.
 *
 * 핵심 개념:
 *   - exec::repeat_n(n)      : void sender를 정확히 n번 반복
 *   - exec::repeat_until()   : bool 값을 반환하는 sender가 true를 반환할 때까지 반복
 *   - exec::repeat()         : 무한 반복 (외부 취소 필요)
 *   - trampoline_scheduler   : 내부적으로 stack overflow 방지에 사용
 *
 * 주의사항:
 *   - repeat_n / repeat 의 sender는 void로 완료해야 함
 *   - repeat_until 의 sender는 bool로 완료해야 함
 *   - 내부적으로 trampoline_scheduler로 stack overflow를 방지함
 *
 * 빌드:
 *   cmake --build build --target playground.repeat
 *   ./build/playground/playground.repeat
 */

#include <stdexec/execution.hpp>

#include <exec/repeat_n.hpp>
#include <exec/repeat_until.hpp>
#include <exec/static_thread_pool.hpp>

#include <atomic>
#include <cstdio>

namespace ex   = stdexec;
namespace exec = experimental::execution;

static void section(char const * title)
{
  std::printf("\n=== %s ===\n", title);
}

///////////////////////////////////////////////////////////////////////////////
// 1. exec::repeat_n: 정확히 N번 반복
//
// void를 완료하는 sender를 N번 반복 실행합니다.
// repeat_n이 완료되면 void로 완료됩니다.
///////////////////////////////////////////////////////////////////////////////
static void example_repeat_n()
{
  section("1. exec::repeat_n - N번 반복");

  int count = 0;

  // just() | then(fn)은 void sender -> repeat_n(5)으로 5번 반복
  auto snd = ex::just() | ex::then([&] { std::printf("  tick %d\n", ++count); })
           | exec::repeat_n(5);

  ex::sync_wait(std::move(snd));
  std::printf("  total count: %d\n", count);  // 5
}

///////////////////////////////////////////////////////////////////////////////
// 2. exec::repeat_until: 조건이 참이 될 때까지 반복
//
// bool 값을 반환하는 sender를 true가 반환될 때까지 반복합니다.
// poll 패턴이나 "준비될 때까지 대기"에 유용합니다.
///////////////////////////////////////////////////////////////////////////////
static void example_repeat_until()
{
  section("2. exec::repeat_until - 조건 만족까지 반복");

  int counter = 0;

  // then이 false를 반환하면 계속 반복, true를 반환하면 종료
  auto snd = ex::just()
           | ex::then(
               [&]
               {
                 ++counter;
                 std::printf("  checking... counter=%d\n", counter);
                 return counter >= 5;  // 5에 도달하면 true -> 종료
               })
           | exec::repeat_until();

  ex::sync_wait(std::move(snd));
  std::printf("  stopped at counter: %d\n", counter);  // 5
}

///////////////////////////////////////////////////////////////////////////////
// 3. repeat_until로 축적(accumulate) 패턴
//
// 루프 상태를 sender 체인 밖에서 mutable 변수로 관리
// 실제 async 루프에서 결과를 누적하는 패턴
///////////////////////////////////////////////////////////////////////////////
static void example_accumulate_pattern()
{
  section("3. repeat_until 축적 패턴");

  std::vector<int> results;
  int              step = 0;

  // 5개의 항목을 비동기적으로 "로드"하는 패턴 시뮬레이션
  auto snd = ex::just()
           | ex::then(
               [&]
               {
                 results.push_back(step * step);  // i^2 누적
                 ++step;
                 return step >= 5;  // 5개 로드 완료 시 종료
               })
           | exec::repeat_until();

  ex::sync_wait(std::move(snd));

  std::printf("  accumulated: [");
  for (std::size_t i = 0; i < results.size(); ++i)
    std::printf("%s%d", i > 0 ? ", " : "", results[i]);
  std::printf("]\n");  // [0, 1, 4, 9, 16]
}

///////////////////////////////////////////////////////////////////////////////
// 4. exec::repeat: 무한 반복 (외부 취소 필요)
//
// repeat()은 sender가 에러나 취소로 완료될 때까지 무한 반복합니다.
// void sender가 stopped로 완료되면 repeat도 stopped로 완료됩니다.
// upon_stopped으로 처리할 수 있습니다.
///////////////////////////////////////////////////////////////////////////////
static void example_repeat_with_stop()
{
  section("4. exec::repeat - 무한 반복 + 취소로 종료");

  // repeat_until과 달리 repeat는 void sender를 받으며 에러/취소까지 무한 반복
  // stopped_as_error로 취소를 에러로 변환하거나
  // upon_stopped으로 취소를 값으로 변환하여 종료

  int count = 0;

  // repeat_n을 활용한 "exactly N iterations" 패턴
  // (repeat 자체는 무한이므로, 유한 반복은 repeat_n이 더 적합)
  auto snd = ex::just()
           | ex::then(
               [&]
               {
                 ++count;
                 std::printf("  repeat: count=%d\n", count);
               })
           | exec::repeat_n(3);  // 3번 반복 후 종료

  ex::sync_wait(std::move(snd));
  std::printf("  final count: %d\n", count);  // 3
}

///////////////////////////////////////////////////////////////////////////////
// 5. 스레드풀 위에서의 repeat
//
// repeat_n + thread pool: 스레드풀에서 N번 반복 작업 수행
// 실제 비동기 처리 파이프라인에서의 반복 패턴
///////////////////////////////////////////////////////////////////////////////
static void example_repeat_on_thread_pool()
{
  section("5. thread pool 위에서 repeat_n");

  exec::static_thread_pool pool{4};
  auto                     sched = pool.get_scheduler();

  std::atomic<int> total{0};

  auto snd = ex::starts_on(sched,
                           ex::just()
                             | ex::then([&] { total.fetch_add(1, std::memory_order_relaxed); })
                             | exec::repeat_n(10));

  ex::sync_wait(std::move(snd));
  std::printf("  total (thread pool, 10 reps): %d\n", total.load());  // 10

  pool.request_stop();
}

///////////////////////////////////////////////////////////////////////////////
// main
///////////////////////////////////////////////////////////////////////////////
auto main() -> int
{
  std::printf("stdexec Playground - repeat 패턴\n");
  std::printf("===================================\n");

  example_repeat_n();
  example_repeat_until();
  example_accumulate_pattern();
  example_repeat_with_stop();
  example_repeat_on_thread_pool();

  std::printf("\n===================================\n");
  std::printf("Done!\n");
  return 0;
}
