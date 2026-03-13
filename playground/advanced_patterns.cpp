/*
 * stdexec Playground - Advanced Patterns (종합)
 *
 * split, ensure_started, finally, create, materialize 등
 * 고급 패턴들을 시연합니다.
 *
 * 패턴 목록:
 *   1. exec::split          : sender를 multicast (여러 번 connect 가능)
 *   2. exec::ensure_started : sender를 즉시(eagerly) 실행하고 결과를 캐싱
 *   3. exec::finally        : 결과와 무관하게 정리 작업 보장
 *   4. exec::create         : 콜백 기반 API를 sender로 래핑
 *   5. exec::materialize    : 모든 completion 채널을 값 채널로 변환
 *
 * 빌드:
 *   cmake --build build --target playground.advanced_patterns
 *   ./build/playground/playground.advanced_patterns
 */

#include <stdexec/execution.hpp>

#include <exec/async_scope.hpp>
#include <exec/create.hpp>
#include <exec/ensure_started.hpp>
#include <exec/finally.hpp>
#include <exec/materialize.hpp>
#include <exec/split.hpp>
#include <exec/start_detached.hpp>
#include <exec/static_thread_pool.hpp>

#include <atomic>
#include <cstdio>
#include <functional>

namespace ex   = stdexec;
namespace exec = experimental::execution;

static void section(char const * title)
{
  std::printf("\n=== %s ===\n", title);
}

///////////////////////////////////////////////////////////////////////////////
// 1. exec::split - Multicast Sender
//
// split()은 sender를 "공유 가능한" sender로 만듭니다.
// 여러 번 connect하고 start할 수 있으며, 원본 작업은 한 번만 실행됩니다.
//
// split vs ensure_started:
//   - split        : lazy (첫 start() 때 실행), copyable
//   - ensure_started: eager (즉시 실행), move-only
///////////////////////////////////////////////////////////////////////////////
static void example_split()
{
  section("1. exec::split - Multicast");

  int compute_count = 0;

  // 비용이 큰 계산을 수행하는 sender
  auto expensive = exec::split(ex::just()
                               | ex::then(
                                 [&]
                                 {
                                   ++compute_count;
                                   std::printf("  computing... (count=%d)\n", compute_count);
                                   return 42;
                                 }));

  // 같은 sender를 세 번 사용 (복사 가능)
  auto use1 = expensive | ex::then([](int x) { return x + 1; });
  auto use2 = expensive | ex::then([](int x) { return x * 2; });
  auto use3 = expensive | ex::then([](int x) { return x - 10; });

  // 각각 독립적으로 실행 가능
  auto [r1] = ex::sync_wait(std::move(use1)).value();
  auto [r2] = ex::sync_wait(std::move(use2)).value();
  auto [r3] = ex::sync_wait(std::move(use3)).value();

  std::printf("  use1=%d, use2=%d, use3=%d\n", r1, r2, r3);  // 43, 84, 32
  std::printf("  compute_count=%d\n", compute_count);        // 1 (한 번만 실행됨)
}

///////////////////////////////////////////////////////////////////////////////
// 2. exec::ensure_started - Eager Execution + Caching
//
// ensure_started()는 sender를 즉시 시작시킵니다.
// 반환된 sender는 결과가 준비될 때까지 기다리다가 결과를 전달합니다.
//
// 사용 사례: "지금 당장 시작하되, 결과는 나중에 필요할 때 가져옴"
///////////////////////////////////////////////////////////////////////////////
static void example_ensure_started()
{
  section("2. exec::ensure_started - 즉시 시작");

  bool started = false;

  // ensure_started는 즉시 실행을 시작
  auto snd1 = ex::just()
            | ex::then(
                [&]
                {
                  started = true;
                  std::printf("  work started eagerly!\n");
                  return 99;
                });

  // ensure_started 호출 시점에 이미 작업 시작
  auto eager = exec::ensure_started(std::move(snd1));
  std::printf("  started after ensure_started: %s\n",
              started ? "true" : "false");  // true

  // 나중에 결과를 소비
  auto [val] = ex::sync_wait(std::move(eager)).value();
  std::printf("  result: %d\n", val);  // 99
}

///////////////////////////////////////////////////////////////////////////////
// 3. exec::finally - 결과와 무관한 정리 보장
//
// finally(work, cleanup)는 work가 어떤 채널로 완료되든
// cleanup을 반드시 실행합니다 (C++의 RAII/finally와 유사).
//
// pipe 형태: work | exec::finally(cleanup)
// 함수 형태: exec::finally(work, cleanup)
///////////////////////////////////////////////////////////////////////////////
static void example_finally()
{
  section("3. exec::finally - 정리 보장");

  // 케이스 1: 정상 완료 시에도 cleanup 실행
  {
    bool cleaned = false;
    auto cleanup = ex::just() | ex::then([&] { cleaned = true; });
    auto work    = ex::just(42) | ex::then([](int x) { return x * 2; });

    auto snd   = exec::finally(std::move(work), std::move(cleanup));
    auto [val] = ex::sync_wait(std::move(snd)).value();
    std::printf("  value: %d, cleaned: %s\n", val,
                cleaned ? "true" : "false");  // 84, true
  }

  // 케이스 2: 에러 발생 시에도 cleanup 실행
  {
    bool cleaned = false;
    auto cleanup = ex::just()
                 | ex::then(
                     [&]
                     {
                       cleaned = true;
                       std::printf("  cleanup runs even on error\n");
                     });

    // upon_error로 먼저 에러를 값으로 변환한 뒤 finally 적용
    auto work = ex::just_error(std::string{"oops"})
              | ex::upon_error([](std::string) { return -1; });

    auto snd   = exec::finally(std::move(work), std::move(cleanup));
    auto [val] = ex::sync_wait(std::move(snd)).value();
    std::printf("  error recovered: %d, cleaned: %s\n",
                val,
                cleaned ? "true" : "false");  // -1, true
  }

  // 케이스 3: pipe 문법으로 사용
  {
    bool cleaned = false;
    auto snd     = ex::just(10) | ex::then([](int x) { return x + 5; })
             | exec::finally(ex::just() | ex::then([&] { cleaned = true; }));
    auto [val] = ex::sync_wait(std::move(snd)).value();
    std::printf("  pipe: value=%d, cleaned=%s\n", val,
                cleaned ? "true" : "false");  // 15, true
  }
}

///////////////////////////////////////////////////////////////////////////////
// 4. exec::create - 콜백 기반 API를 Sender로 래핑
//
// C 스타일 콜백이나 레거시 비동기 API를 sender로 변환할 때 사용합니다.
// create<completion_signatures>(fn)에서 fn은 context를 받아 콜백을 등록합니다.
// context.receiver에 set_value, set_error, set_stopped를 호출하여 완료합니다.
///////////////////////////////////////////////////////////////////////////////

// 레거시 콜백 기반 API 시뮬레이션
static void legacy_add_async(int a, int b, void* ctx, void (*cb)(void*, int))
{
  // 실제로는 스레드나 I/O 이벤트로 완료하겠지만, 여기서는 즉시 콜백 호출
  cb(ctx, a + b);
}

static void example_create()
{
  section("4. exec::create - 콜백 API 래핑");

  // 콜백 기반 API를 sender로 래핑
  auto snd = [](int a, int b)
  {
    return exec::create<ex::set_value_t(int)>(
      [a, b]<class Context>(Context& ctx) noexcept
      {
        // 레거시 API에 context를 userdata로 전달
        legacy_add_async(a,
                         b,
                         &ctx,
                         [](void* pv, int result)
                         {
                           // 콜백 안에서 receiver를 통해 완료
                           auto& ctx = *static_cast<Context*>(pv);
                           ex::set_value(std::move(ctx.receiver), result);
                         });
      });
  }(3, 7);

  auto [result] = ex::sync_wait(std::move(snd)).value();
  std::printf("  legacy_add_async(3, 7) via create = %d\n", result);  // 10

  // 여러 인자를 전달하는 버전 (ctx.args로 접근)
  bool flag = false;
  auto snd2 = exec::create<ex::completion_signatures<ex::set_value_t()>>(
    [](auto& ctx) noexcept
    {
      // ctx.args는 tuple로 추가 인자를 보관
      *std::get<0>(ctx.args) = true;
      ex::set_value(std::move(ctx.receiver));
    },
    &flag);  // 추가 인자: flag 포인터

  ex::sync_wait(std::move(snd2));
  std::printf("  flag set via create args: %s\n",
              flag ? "true" : "false");  // true
}

///////////////////////////////////////////////////////////////////////////////
// 5. exec::materialize / exec::dematerialize
//
// materialize: 모든 completion 채널을 "값 채널"로 변환
//   set_value(args...)  -> set_value(set_value_t{}, args...)
//   set_error(err)      -> set_value(set_error_t{}, err)
//   set_stopped()       -> set_value(set_stopped_t{})
//
// 활용: 모든 완료를 균일하게 처리하는 generic 알고리즘 작성에 유용
// dematerialize: 반대 방향 변환
///////////////////////////////////////////////////////////////////////////////
static void example_materialize()
{
  section("5. exec::materialize / dematerialize");

  // 값 완료: set_value(42) -> set_value(set_value_t{}, 42)
  {
    auto snd      = exec::materialize(ex::just(42));
    auto [tag, v] = ex::sync_wait(std::move(snd)).value();
    // tag의 타입: set_value_t
    std::printf("  materialize(just(42)): tag=%s, value=%d\n", typeid(tag).name(), v);
  }

  // 취소 완료: set_stopped() -> set_value(set_stopped_t{})
  {
    auto snd   = exec::materialize(ex::just_stopped());
    auto [tag] = ex::sync_wait(std::move(snd)).value();
    // tag의 타입: set_stopped_t
    (void) tag;
    std::printf("  materialize(just_stopped()): got stopped as value\n");
  }

  // round-trip: dematerialize(materialize(s)) == s
  {
    auto snd   = exec::dematerialize(exec::materialize(ex::just(99)));
    auto [val] = ex::sync_wait(std::move(snd)).value();
    std::printf("  dematerialize(materialize(just(99))) => %d\n", val);  // 99
  }

  // 실용 예: 모든 완료 채널을 값으로 통일하여 공통 처리
  {
    // error sender를 materialize해서 통일된 처리
    auto snd = exec::materialize(ex::just_error(std::string{"err"}));
    // 이제 모든 완료가 값으로 오므로, then으로 처리 가능
    auto snd2 = std::move(snd)
              | ex::then(
                  [](auto tag, auto... args)
                  {
                    (void) tag;
                    ((void) args, ...);
                    std::printf("  materialize: all completions handled uniformly\n");
                    return 0;
                  });
    ex::sync_wait(std::move(snd2));
  }
}

///////////////////////////////////////////////////////////////////////////////
// main
///////////////////////////////////////////////////////////////////////////////
auto main() -> int
{
  std::printf("stdexec Playground - Advanced Patterns\n");
  std::printf("========================================\n");

  example_split();
  example_ensure_started();
  example_finally();
  example_create();
  example_materialize();

  std::printf("\n========================================\n");
  std::printf("Done!\n");
  return 0;
}
