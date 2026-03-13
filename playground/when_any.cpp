/*
 * stdexec Playground - when_any 패턴 (Advanced)
 *
 * exec::when_any는 여러 sender를 동시에 실행하고,
 * 가장 먼저 완료되는 sender의 결과를 채택하는 "경쟁(race)" 알고리즘입니다.
 * 나머지 sender들은 자동으로 취소됩니다.
 *
 * 핵심 개념:
 *   - exec::when_any(senders...) : 첫 번째 완료 sender의 값을 반환
 *   - 에러/취소 처리: 모든 sender가 에러/취소로 완료되는 경우의 동작
 *   - 타임아웃 패턴: when_any + 지연 sender로 timeout 구현
 *   - 실용 패턴: 중복 요청 중 첫 응답 채택
 *
 * 빌드:
 *   cmake --build build --target playground.when_any
 *   ./build/playground/playground.when_any
 */

#include <stdexec/execution.hpp>

#include <exec/single_thread_context.hpp>
#include <exec/static_thread_pool.hpp>
#include <exec/when_any.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

namespace ex   = stdexec;
namespace exec = experimental::execution;

static void section(char const * title)
{
  std::printf("\n=== %s ===\n", title);
}

///////////////////////////////////////////////////////////////////////////////
// 1. exec::when_any 기본: 첫 완료가 승자
//
// 모든 sender가 동시에 시작되고, 가장 먼저 완료되는 sender의
// 값이 결과로 채택됩니다. 나머지는 stop token으로 취소됩니다.
//
// 주의: 모든 value 타입은 decay-copyable이어야 함 (값이 복사될 수 있음)
///////////////////////////////////////////////////////////////////////////////
static void example_when_any_basic()
{
  section("1. exec::when_any 기본");

  exec::single_thread_context fast_ctx, slow_ctx;

  std::atomic<bool> slow_ran{false};

  // fast_ctx에서 즉시 완료
  auto fast = ex::starts_on(fast_ctx.get_scheduler(), ex::just(42));

  // slow_ctx에서 늦게 완료 (취소될 것)
  auto slow = ex::starts_on(slow_ctx.get_scheduler(),
                            ex::just()
                              | ex::then(
                                [&]
                                {
                                  std::this_thread::sleep_for(std::chrono::milliseconds(50));
                                  slow_ran.store(true, std::memory_order_relaxed);
                                  return 99;
                                }));

  // 먼저 완료되는 쪽의 값을 채택
  auto [winner] = ex::sync_wait(exec::when_any(std::move(fast), std::move(slow))).value();
  std::printf("  winner: %d\n", winner);  // 42 (fast가 먼저 완료)
  // slow는 stop 요청을 받았지만 sleep 중이라 취소 불가 -> 완료까지 대기
}

///////////////////////////////////////////////////////////////////////////////
// 2. when_any로 타임아웃 패턴 구현
//
// 실제 작업 sender와 "timeout" sender를 경쟁시켜
// 정해진 시간 내에 완료되지 않으면 타임아웃 처리
//
// 여기서는 timed_thread_scheduler 없이 간단히 스레드 sleep으로 시뮬레이션합니다.
///////////////////////////////////////////////////////////////////////////////

// 지연 후 완료하는 sender를 만드는 헬퍼
auto delayed_value(exec::single_thread_context& ctx, std::chrono::milliseconds delay, int value)
{
  return ex::starts_on(ctx.get_scheduler(),
                       ex::just(value)
                         | ex::then(
                           [delay](int v)
                           {
                             std::this_thread::sleep_for(delay);
                             return v;
                           }));
}

// 타임아웃 sentinel 타입
struct timeout_t
{};

static void example_timeout_pattern()
{
  section("2. when_any 타임아웃 패턴");

  exec::single_thread_context work_ctx, timer_ctx;

  // 케이스 1: 작업이 타임아웃보다 빠름 -> 작업 결과 반환
  {
    // 작업: 10ms 후 42 반환
    auto work = delayed_value(work_ctx, std::chrono::milliseconds(10), 42)
              | ex::then([](int v) -> std::variant<int, timeout_t> { return v; });

    // 타임아웃: 100ms 후 timeout_t 반환
    auto timeout = delayed_value(timer_ctx, std::chrono::milliseconds(100), 0)
                 | ex::then([](int) -> std::variant<int, timeout_t> { return timeout_t{}; });

    auto [result] = ex::sync_wait(exec::when_any(std::move(work), std::move(timeout))).value();

    std::visit(
      [](auto v)
      {
        if constexpr (std::is_same_v<decltype(v), int>)
          std::printf("  case1: got result = %d\n", v);
        else
          std::printf("  case1: timeout!\n");
      },
      result);
  }

  // 케이스 2: 작업이 타임아웃보다 느림 -> 타임아웃
  {
    auto work = delayed_value(work_ctx, std::chrono::milliseconds(100), 42)
              | ex::then([](int v) -> std::variant<int, timeout_t> { return v; });

    auto timeout = delayed_value(timer_ctx, std::chrono::milliseconds(10), 0)
                 | ex::then([](int) -> std::variant<int, timeout_t> { return timeout_t{}; });

    auto [result] = ex::sync_wait(exec::when_any(std::move(work), std::move(timeout))).value();

    std::visit(
      [](auto v)
      {
        if constexpr (std::is_same_v<decltype(v), int>)
          std::printf("  case2: got result = %d\n", v);
        else
          std::printf("  case2: timeout!\n");
      },
      result);
  }
}

///////////////////////////////////////////////////////////////////////////////
// 3. when_any로 중복 서버 요청 패턴 (Redundant Request)
//
// 같은 요청을 여러 서버에 동시에 보내고, 첫 번째 응답만 사용
// 네트워크 지연이 불확실할 때 낮은 tail latency를 보장하는 기법
///////////////////////////////////////////////////////////////////////////////
static void example_redundant_request()
{
  section("3. 중복 요청 패턴 (Redundant Request)");

  exec::static_thread_pool pool{4};
  auto                     sched = pool.get_scheduler();

  // 세 개의 "서버" 시뮬레이션: 각각 다른 지연 시간
  auto make_server_request = [&](int server_id, std::chrono::milliseconds latency)
  {
    return ex::starts_on(sched,
                         ex::just(server_id, latency.count())
                           | ex::then(
                             [](int id, long ms)
                             {
                               std::this_thread::sleep_for(std::chrono::milliseconds(ms));
                               std::printf("  server %d responded after %ldms\n", id, ms);
                               return id * 100;  // 서버 ID * 100을 응답값으로
                             }));
  };

  // 세 서버에 동시 요청: 가장 빠른 응답 채택
  auto [response] =
    ex::sync_wait(
      exec::when_any(make_server_request(1, std::chrono::milliseconds(50)),
                     make_server_request(2, std::chrono::milliseconds(20)),  // 이게 가장 빠름
                     make_server_request(3, std::chrono::milliseconds(80))))
      .value();

  std::printf("  fastest response: %d\n", response);  // 200 (server 2)

  pool.request_stop();
}

///////////////////////////////////////////////////////////////////////////////
// 4. when_any와 에러/취소 처리
//
// - 하나가 에러로 완료 -> 나머지 취소 후 에러 전파
// - 모두 에러 -> 마지막 에러가 전파
// - 부모가 stop 요청 -> set_stopped() 반환
///////////////////////////////////////////////////////////////////////////////
static void example_when_any_error()
{
  section("4. when_any 에러 처리");

  exec::single_thread_context ctx1, ctx2;

  // 케이스: 모든 sender가 에러 -> when_any도 에러로 완료
  auto err1 = ex::starts_on(ctx1.get_scheduler(), ex::just_error(std::string{"error from ctx1"}));
  auto err2 = ex::starts_on(ctx2.get_scheduler(), ex::just_error(std::string{"error from ctx2"}));

  auto snd = exec::when_any(std::move(err1), std::move(err2))
           | ex::let_error(
               [](auto&)
               {
                 std::printf("  all senders failed -> let_error recovered\n");
                 return ex::just(-1);
               });

  auto [val] = ex::sync_wait(std::move(snd)).value();
  std::printf("  when_any(all errors) recovered => %d\n", val);
}

///////////////////////////////////////////////////////////////////////////////
// main
///////////////////////////////////////////////////////////////////////////////
auto main() -> int
{
  std::printf("stdexec Playground - when_any 패턴\n");
  std::printf("=====================================\n");

  example_when_any_basic();
  example_timeout_pattern();
  example_redundant_request();
  example_when_any_error();

  std::printf("\n=====================================\n");
  std::printf("Done!\n");
  return 0;
}
