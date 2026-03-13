/*
 * stdexec Playground - 취소(Cancellation) 패턴 (Advanced)
 *
 * stdexec의 취소 모델은 stop token을 통해 동작합니다.
 * 부모가 자식에게 stop token을 환경(environment)을 통해 전달하고,
 * 자식은 이를 확인하거나 stop callback을 등록해 취소에 반응합니다.
 *
 * 핵심 개념:
 *   - inplace_stop_source  : stop 신호를 발생시키는 객체
 *   - inplace_stop_token   : stop 상태를 확인하는 토큰
 *   - inplace_stop_callback: stop 발생 시 호출되는 콜백 등록
 *   - when_all 자동 취소   : 하나가 에러/취소되면 나머지도 취소
 *   - exec::unless_stop_requested : 이미 stop된 경우 실행 skip
 *
 * 빌드:
 *   cmake --build build --target playground.cancellation
 *   ./build/playground/playground.cancellation
 */

#include <stdexec/execution.hpp>

#include <exec/async_scope.hpp>
#include <exec/single_thread_context.hpp>
#include <exec/static_thread_pool.hpp>
#include <exec/unless_stop_requested.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

namespace ex = stdexec;

static void section(char const * title)
{
  std::printf("\n=== %s ===\n", title);
}

///////////////////////////////////////////////////////////////////////////////
// 1. inplace_stop_source / inplace_stop_token 기본 사용
//
// stop_source: stop 신호를 발생시키는 제어 객체
// stop_token: stop 상태를 확인하는 읽기 전용 토큰
// stop_callback: stop 발생 시 즉시 호출되는 콜백
///////////////////////////////////////////////////////////////////////////////
static void example_stop_token_basics()
{
  section("1. inplace_stop_source / inplace_stop_token 기본");

  ex::inplace_stop_source source;
  ex::inplace_stop_token  token = source.get_token();

  std::printf("  stop_requested (before): %s\n",
              token.stop_requested() ? "true" : "false");  // false

  // stop_callback 등록: stop 발생 시 즉시 호출
  bool callback_called = false;
  // inplace_stop_callback은 명시적 템플릿 인자가 필요
  ex::inplace_stop_callback<std::function<void()>> cb{token,
                                                      [&]
                                                      {
                                                        callback_called = true;
                                                        std::printf("  stop_callback invoked!\n");
                                                      }};

  // stop 요청
  source.request_stop();

  std::printf("  stop_requested (after): %s\n",
              token.stop_requested() ? "true" : "false");  // true
  std::printf("  callback_called: %s\n",
              callback_called ? "true" : "false");  // true
}

///////////////////////////////////////////////////////////////////////////////
// 2. sync_wait와 stop token: 취소 시 nullopt 반환
//
// sync_wait는 stop token이 환경에 있으면 자식 sender에게 전달
// sender가 set_stopped()로 완료되면 sync_wait는 nullopt 반환
///////////////////////////////////////////////////////////////////////////////
static void example_sync_wait_stopped()
{
  section("2. sync_wait 취소 시 nullopt 반환");

  // just_stopped(): 즉시 set_stopped()로 완료하는 sender
  // sync_wait는 stopped 완료 시 nullopt를 반환 (값 완료 없이도 OK)
  auto result = ex::sync_wait(ex::just_stopped() | ex::upon_stopped([] { return 0; }));
  std::printf("  just_stopped() => %s\n", result.has_value() ? "has value" : "nullopt (cancelled)");

  // stopped_as_optional: 값 채널이 있는 sender에서 stopped를 optional<T>로 변환
  // just_stopped()에는 값 채널이 없으므로, 값을 가진 sender에 붙여야 함
  auto snd   = ex::just(42) | ex::stopped_as_optional();
  auto [opt] = ex::sync_wait(std::move(snd)).value();
  std::printf("  just(42)|stopped_as_optional => %d\n", opt.value());
}

///////////////////////////////////////////////////////////////////////////////
// 3. when_all의 자동 취소 전파
//
// when_all은 하나의 자식이 에러/취소로 완료되면
// 나머지 자식들에게 stop token을 통해 자동으로 취소를 요청합니다.
///////////////////////////////////////////////////////////////////////////////
static void example_when_all_cancellation()
{
  section("3. when_all 자동 취소 전파");

  std::atomic<int> ran{0};

  exec::single_thread_context ctx1, ctx2;

  // ctx1에서는 빠르게 에러로 완료
  // ctx2에서는 stop_token을 확인하며 실행 (stop 요청 시 set_stopped)
  auto slow_work = ex::starts_on(ctx2.get_scheduler(),
                                 ex::just()
                                   | ex::then(
                                     [&]
                                     {
                                       // stop이 요청되었는지 확인
                                       std::this_thread::sleep_for(std::chrono::milliseconds(10));
                                       ran.fetch_add(1, std::memory_order_relaxed);
                                     }));

  auto fast_error = ex::starts_on(ctx1.get_scheduler(), ex::just_error(std::string{"fast error"}));

  // when_all: fast_error가 에러로 완료 -> slow_work에 stop 전파
  // starts_on은 exception_ptr도 에러 채널에 추가하므로
  // let_error로 모든 에러 타입을 처리
  auto snd = ex::when_all(std::move(fast_error), std::move(slow_work))
           | ex::let_error(
               [](auto&)
               {
                 std::printf("  when_all: one child failed, others cancelled\n");
                 return ex::just(0);
               });

  ex::sync_wait(std::move(snd));
}

///////////////////////////////////////////////////////////////////////////////
// 4. exec::unless_stop_requested
//
// 이미 stop이 요청된 상태에서 sender를 실행하려 하면
// 실제 실행 없이 즉시 set_stopped()로 완료합니다.
// "stop이 요청됐으면 이 작업은 건너뜀"을 명시적으로 표현
///////////////////////////////////////////////////////////////////////////////
static void example_unless_stop_requested()
{
  section("4. exec::unless_stop_requested");

  // 케이스 1: stop이 요청되지 않은 경우 -> 정상 실행
  {
    bool executed = false;
    auto snd      = ex::just() | ex::then([&] { executed = true; }) | exec::unless_stop_requested;
    ex::sync_wait(std::move(snd));
    std::printf("  without stop: executed = %s\n",
                executed ? "true" : "false");  // true
  }

  // 케이스 2: stop이 이미 요청된 경우 -> 실행 skip, set_stopped() 반환
  {
    ex::inplace_stop_source source;
    source.request_stop();  // 미리 stop 요청

    bool executed = false;

    // stop token을 환경에 주입하는 방법:
    // prop(get_stop_token, token)으로 환경 커스터마이징
    auto env = ex::prop(ex::get_stop_token, source.get_token());

    // write_env: 기존 sender에 환경을 덧씌우는 유틸리티
    auto snd = ex::write_env(ex::just() | ex::then([&] { executed = true; })
                               | exec::unless_stop_requested,
                             env);

    auto result = ex::sync_wait(std::move(snd));
    std::printf("  with stop: executed = %s, result = %s\n",
                executed ? "true" : "false",                          // false
                result.has_value() ? "value" : "nullopt (stopped)");  // nullopt
  }
}

///////////////////////////////////////////////////////////////////////////////
// 5. async_scope에서의 취소 패턴
//
// async_scope에 spawn된 작업들은 scope가 on_empty()를 await하는 동안
// 외부에서 stop을 요청하면 모두 취소될 수 있습니다.
///////////////////////////////////////////////////////////////////////////////
static void example_scope_cancellation()
{
  section("5. async_scope + 외부 취소");

  exec::static_thread_pool pool{4};
  auto                     sched = pool.get_scheduler();
  exec::async_scope        scope;

  std::atomic<int> completed{0};

  // 여러 작업을 spawn
  for (int i = 0; i < 5; ++i)
  {
    scope.spawn(ex::starts_on(sched,
                              ex::just(i)
                                | ex::then(
                                  [&completed](int idx)
                                  {
                                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                                    completed.fetch_add(1, std::memory_order_relaxed);
                                    std::printf("  task %d completed\n", idx);
                                  })));
  }

  // 모든 작업 완료 대기
  ex::sync_wait(scope.on_empty());
  std::printf("  total completed: %d\n", completed.load());  // 5

  pool.request_stop();
}

///////////////////////////////////////////////////////////////////////////////
// main
///////////////////////////////////////////////////////////////////////////////
auto main() -> int
{
  std::printf("stdexec Playground - 취소(Cancellation) 패턴\n");
  std::printf("===============================================\n");

  example_stop_token_basics();
  example_sync_wait_stopped();
  example_when_all_cancellation();
  example_unless_stop_requested();
  example_scope_cancellation();

  std::printf("\n===============================================\n");
  std::printf("Done!\n");
  return 0;
}
