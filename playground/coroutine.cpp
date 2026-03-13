/*
 * stdexec Playground - 코루틴 패턴 (Advanced)
 *
 * exec::task<T> 는 stdexec의 코루틴 기반 비동기 task 타입입니다.
 * C++20 코루틴을 통해 sender를 co_await할 수 있으며,
 * 스케줄러 친화성(scheduler affinity), stop token 전파 등을 자동으로 처리합니다.
 *
 * 핵심 개념:
 *   - exec::task<T>    : co_await 가능한 비동기 task. sender이기도 함
 *   - co_await sender  : task 내에서 임의의 sender를 await 가능
 *   - Scheduler stickiness : co_await 후 자동으로 원래 스케줄러로 복귀
 *   - exec::reschedule_coroutine_on : 명시적으로 다른 스케줄러로 전환
 *   - exec::at_coroutine_exit       : 코루틴 종료 시 정리 작업 등록 (LIFO)
 *
 * 빌드:
 *   cmake --build build --target playground.coroutine
 *   ./build/playground/playground.coroutine
 */

#include <stdexec/execution.hpp>

#if !STDEXEC_NO_STDCPP_COROUTINES()

#  include <exec/at_coroutine_exit.hpp>
#  include <exec/single_thread_context.hpp>
#  include <exec/static_thread_pool.hpp>
#  include <exec/task.hpp>

#  include <cstdio>
#  include <string>

namespace ex = stdexec;

///////////////////////////////////////////////////////////////////////////////
// 헬퍼
///////////////////////////////////////////////////////////////////////////////
static void section(char const * title)
{
  std::printf("\n=== %s ===\n", title);
}

///////////////////////////////////////////////////////////////////////////////
// 1. exec::task 기본: co_await sender, co_return
//
// exec::task<T>는 C++20 코루틴으로, 내부에서 임의의 sender를 co_await 할 수 있음
// task 자체도 sender이므로 sync_wait()으로 실행 가능
///////////////////////////////////////////////////////////////////////////////
static auto task_basic() -> exec::task<int>
{
  // sender를 co_await: 완료될 때까지 비동기 대기
  co_await ex::just();            // void sender await
  int x = co_await ex::just(10);  // 값을 가진 sender await
  int y = co_await ex::just(32);
  co_return x + y;  // 42
}

static void example_task_basic()
{
  section("1. exec::task 기본 사용");

  // task는 sender이므로 sync_wait으로 실행
  auto [result] = ex::sync_wait(task_basic()).value();
  std::printf("task_basic() => %d\n", result);  // 42
}

///////////////////////////////////////////////////////////////////////////////
// 2. task 체이닝: task 안에서 다른 task를 co_await
//
// task는 sender이므로, 다른 task 안에서 co_await 할 수 있음
// 자연스럽게 비동기 함수 호출처럼 사용 가능
///////////////////////////////////////////////////////////////////////////////
static auto fetch_data(int id) -> exec::task<std::string>
{
  // 실제 환경에서는 I/O, 네트워크 요청 등이 들어갈 자리
  co_await ex::just();  // 비동기 작업 시뮬레이션
  co_return "data_" + std::to_string(id);
}

static auto process_data(std::string raw) -> exec::task<std::string>
{
  co_await ex::just();
  co_return "[processed: " + raw + "]";
}

static auto pipeline_task() -> exec::task<std::string>
{
  // task 체이닝: 각 단계를 co_await으로 순차 실행
  auto raw       = co_await fetch_data(42);
  auto processed = co_await process_data(raw);
  co_return processed;
}

static void example_task_chaining()
{
  section("2. task 체이닝");

  auto [result] = ex::sync_wait(pipeline_task()).value();
  std::printf("pipeline_task() => %s\n", result.c_str());
}

///////////////////////////////////////////////////////////////////////////////
// 3. Scheduler stickiness (스케줄러 친화성)
//
// exec::task는 특정 스케줄러와 연관됩니다.
// co_await로 다른 스케줄러에서 실행되는 sender를 await하더라도,
// await 완료 후 자동으로 원래 스케줄러로 복귀합니다.
//
// 이 동작은 thread safety를 자연스럽게 보장해줍니다:
// task의 로직은 항상 같은 스케줄러(스레드) 위에서 실행됨
///////////////////////////////////////////////////////////////////////////////
static auto sticky_task(exec::static_thread_pool& pool) -> exec::task<void>
{
  auto pool_sched = pool.get_scheduler();

  // 현재 스케줄러(task가 시작된 스레드)를 확인
  std::printf("  [before] thread: %p\n", static_cast<void*>(pthread_self()));

  // pool 스케줄러에서 실행되는 작업을 await
  // 이 작업은 pool 스레드 위에서 실행됨
  co_await ex::starts_on(pool_sched,
                         ex::just()
                           | ex::then(
                             []
                             {
                               std::printf("  [inside starts_on] thread: %p\n",
                                           static_cast<void*>(pthread_self()));
                             }));

  // co_await 완료 후 자동으로 원래 스케줄러(task의 스케줄러)로 복귀
  std::printf("  [after] thread: %p\n", static_cast<void*>(pthread_self()));
}

static void example_scheduler_stickiness()
{
  section("3. Scheduler stickiness (자동 스케줄러 복귀)");

  exec::single_thread_context ctx;  // task를 실행할 단일 스레드 컨텍스트
  exec::static_thread_pool    pool{4};

  // task를 ctx의 스케줄러 위에서 시작
  ex::sync_wait(ex::starts_on(ctx.get_scheduler(), sticky_task(pool)));

  std::printf("  (위에서 [before]와 [after]의 thread ID가 같음)\n");
  pool.request_stop();
}

///////////////////////////////////////////////////////////////////////////////
// 4. exec::reschedule_coroutine_on
//
// task 내에서 명시적으로 다른 스케줄러로 전환하고 싶을 때 사용
// 전환 후에는 stickiness가 새 스케줄러로 바뀜 (원래 스케줄러로 자동 복귀하지 않음)
///////////////////////////////////////////////////////////////////////////////
static auto reschedule_task(exec::static_thread_pool& pool) -> exec::task<int>
{
  std::printf("  step 1 - thread: %p\n", static_cast<void*>(pthread_self()));

  // pool 스케줄러로 명시적 전환
  // 이후의 작업은 pool 스레드에서 실행됨
  co_await exec::reschedule_coroutine_on(pool.get_scheduler());

  std::printf("  step 2 (on pool) - thread: %p\n", static_cast<void*>(pthread_self()));

  // 계산 수행 (sender를 먼저 만들고 co_await)
  int result = co_await (ex::just(100) | ex::then([](int x) { return x + 1; }));

  std::printf("  step 3 (still on pool) - thread: %p\n", static_cast<void*>(pthread_self()));
  co_return result;
}

static void example_reschedule()
{
  section("4. exec::reschedule_coroutine_on");

  exec::single_thread_context ctx;
  exec::static_thread_pool    pool{2};

  auto [val] = ex::sync_wait(ex::starts_on(ctx.get_scheduler(), reschedule_task(pool))).value();
  std::printf("  result: %d\n", val);  // 101

  pool.request_stop();
}

///////////////////////////////////////////////////////////////////////////////
// 5. exec::at_coroutine_exit - 코루틴 종료 시 정리 작업
//
// at_coroutine_exit은 코루틴이 종료될 때 (성공/에러/취소 모두) 실행되는
// 정리 작업을 등록합니다. LIFO 순서로 실행됩니다 (C++ 소멸자와 동일한 순서).
//
// 주의: 정리 task 안에서 co_await ex::just_stopped() 하면 std::terminate 호출
///////////////////////////////////////////////////////////////////////////////
static auto cleanup_task() -> exec::task<int>
{
  int result = 0;

  // 첫 번째 정리 작업 등록 (나중에 실행됨 - LIFO)
  co_await exec::at_coroutine_exit(
    [&result]() -> exec::task<void>
    {
      std::printf("  [cleanup 1] result at exit: %d\n", result);
      co_return;
    });

  // 두 번째 정리 작업 등록 (먼저 실행됨 - LIFO)
  co_await exec::at_coroutine_exit(
    [&result]() -> exec::task<void>
    {
      result *= 10;
      std::printf("  [cleanup 2] multiplied: %d\n", result);
      co_return;
    });

  result = 7;
  std::printf("  [main] result set to: %d\n", result);
  co_return result;  // 70이 리턴됨 (cleanup 2: *10, 그 후 cleanup 1: 출력)
}

static void example_at_coroutine_exit()
{
  section("5. exec::at_coroutine_exit (LIFO 정리)");

  auto [val] = ex::sync_wait(cleanup_task()).value();
  std::printf("  final result: %d\n", val);  // 7 (정리 작업은 완료 후 실행)
}

///////////////////////////////////////////////////////////////////////////////
// 6. task에서 에러/취소 처리
//
// task 내에서 에러가 발생하면 exception이 전파됩니다.
// stopped_as_optional()로 취소를 optional로 변환하여 처리할 수 있습니다.
///////////////////////////////////////////////////////////////////////////////
static auto may_fail(bool succeed) -> exec::task<int>
{
  if (!succeed)
  {
    // 에러를 던지면 exec::task가 set_error(exception_ptr)로 변환
    throw std::runtime_error{"task failed!"};
  }
  co_return 42;
}

static auto safe_task(bool succeed) -> exec::task<int>
{
  STDEXEC_TRY
  {
    int val = co_await may_fail(succeed);
    co_return val;
  }
  STDEXEC_CATCH(std::exception & e)
  {
    std::printf("  caught in task: %s\n", e.what());
    co_return -1;
  }
}

static void example_task_error_handling()
{
  section("6. task 에러 처리");

  auto [ok] = ex::sync_wait(safe_task(true)).value();
  std::printf("  succeed=true  => %d\n", ok);  // 42

  auto [fail] = ex::sync_wait(safe_task(false)).value();
  std::printf("  succeed=false => %d\n", fail);  // -1
}

///////////////////////////////////////////////////////////////////////////////
// main
///////////////////////////////////////////////////////////////////////////////
auto main() -> int
{
  std::printf("stdexec Playground - 코루틴 패턴\n");
  std::printf("==================================\n");

  example_task_basic();
  example_task_chaining();
  example_scheduler_stickiness();
  example_reschedule();
  example_at_coroutine_exit();
  example_task_error_handling();

  std::printf("\n==================================\n");
  std::printf("Done!\n");
  return 0;
}

#else

auto main() -> int
{
  std::printf("이 컴파일러는 C++20 코루틴을 지원하지 않습니다.\n");
  return 0;
}

#endif
