/*
 * stdexec Playground - 커스텀 Sender/Receiver 구현 (Advanced)
 *
 * stdexec의 핵심 확장 포인트는 커스텀 sender와 receiver를 직접 구현하는 것입니다.
 * 이를 통해 기존 callback/promise 기반 API를 sender로 래핑하거나,
 * 특수한 동작(재시도, 타임아웃, 리소스 관리 등)을 가진 새로운 알고리즘을 만들 수 있습니다.
 *
 * 핵심 개념:
 *   - 커스텀 sender : sender_concept 태그 + completion_signatures + connect()
 *   - 커스텀 receiver: receiver_concept 태그 + set_value/set_error/set_stopped
 *   - exec::receiver_adaptor : CRTP로 기존 receiver를 래핑하는 헬퍼
 *   - operation state : connect(sender, receiver) 의 결과. start()로 실행
 *   - retry 패턴 : 에러 시 operation을 재시작
 *
 * 빌드:
 *   cmake --build build --target playground.custom_sender
 *   ./build/playground/playground.custom_sender
 */

#include <stdexec/execution.hpp>

#include <exec/receiver_adaptor.hpp>
#include <exec/static_thread_pool.hpp>

#include <cstdio>
#include <optional>

namespace ex = stdexec;

static void section(char const * title)
{
  std::printf("\n=== %s ===\n", title);
}

///////////////////////////////////////////////////////////////////////////////
// 1. 가장 간단한 커스텀 Sender
//
// Sender가 되기 위해 필요한 최소한의 요소:
//   1. `using sender_concept = stdexec::sender_t;`  -- "나는 sender다" 태그
//   2. `completion_signatures` -- 어떻게 완료될 수 있는지 선언
//   3. `connect(Receiver)` -- operation state를 생성하여 반환
//
// Operation State가 되기 위한 요소:
//   1. `start() & noexcept` -- 비동기 작업을 실제로 시작
///////////////////////////////////////////////////////////////////////////////

// 항상 고정된 값으로 완료하는 간단한 sender
struct always_42_sender
{
  using sender_concept = ex::sender_t;
  using completion_signatures =
    ex::completion_signatures<ex::set_value_t(int)  // 값 채널: int 하나를 완료 값으로 보냄
                              >;

  // Operation State: receiver를 보유하고 start()에서 완료시킴
  template <class Receiver>
  struct op
  {
    Receiver rcvr_;

    void start() & noexcept
    {
      // 동기적으로 즉시 완료 (실제 비동기 작업에서는 비동기 작업을 여기서 시작)
      ex::set_value(static_cast<Receiver&&>(rcvr_), 42);
    }
  };

  template <ex::receiver_of<completion_signatures> Receiver>
  auto connect(Receiver rcvr) const -> op<Receiver>
  {
    return {static_cast<Receiver&&>(rcvr)};
  }
};

static void example_minimal_sender()
{
  section("1. 최소 커스텀 Sender");

  always_42_sender snd;

  // 커스텀 sender도 일반 sender처럼 사용 가능
  auto [val] = ex::sync_wait(snd).value();
  std::printf("always_42_sender => %d\n", val);

  // pipe adaptor도 사용 가능
  auto [doubled] = ex::sync_wait(snd | ex::then([](int x) { return x * 2; })).value();
  std::printf("always_42_sender | then(*2) => %d\n", doubled);
}

///////////////////////////////////////////////////////////////////////////////
// 2. 에러를 생성하는 커스텀 Sender
//
// 여러 completion 채널을 갖는 sender 예제
// 카운터에 따라 값 또는 에러로 완료
///////////////////////////////////////////////////////////////////////////////
struct flaky_sender
{
  using sender_concept = ex::sender_t;
  using completion_signatures =
    ex::completion_signatures<ex::set_value_t(int),
                              ex::set_error_t(std::string)  // 에러 채널: string 에러
                              >;

  int  attempt_ = 0;
  int* counter_;  // 외부 카운터 포인터

  template <class Receiver>
  struct op
  {
    Receiver rcvr_;
    int      attempt_;
    int*     counter_;

    void start() & noexcept
    {
      int current = ++(*counter_);
      if (current < attempt_)
      {
        std::printf("  attempt %d: fail\n", current);
        ex::set_error(static_cast<Receiver&&>(rcvr_), std::string{"transient error"});
      }
      else
      {
        std::printf("  attempt %d: success\n", current);
        ex::set_value(static_cast<Receiver&&>(rcvr_), current * 10);
      }
    }
  };

  template <class Receiver>
  auto connect(Receiver rcvr) const -> op<Receiver>
  {
    return {static_cast<Receiver&&>(rcvr), attempt_, counter_};
  }
};

static void example_multi_channel_sender()
{
  section("2. 에러/값 다중 채널 Sender");

  int counter = 0;
  // 3번째 시도에서 성공하는 sender
  flaky_sender snd{.attempt_ = 3, .counter_ = &counter};

  // 첫 시도는 에러 -> upon_error로 복구
  auto [val] = ex::sync_wait(snd | ex::upon_error([](std::string) { return -1; })).value();
  std::printf("  result (1st try, fails): %d\n", val);  // -1

  // 카운터 리셋하고 2번째 시도
  counter   = 0;
  auto snd2 = flaky_sender{.attempt_ = 3, .counter_ = &counter};
  // let_error로 재시도 패턴 (간단 버전)
  auto result = ex::sync_wait(snd2 | ex::upon_error([](std::string) { return 0; }));
  std::printf("  result (1st try of snd2): %d\n", result.value() == std::tuple{0} ? 0 : -99);
}

///////////////////////////////////////////////////////////////////////////////
// 3. exec::receiver_adaptor 를 활용한 커스텀 Receiver
//
// receiver_adaptor<Derived>는 CRTP 기반의 편의 헬퍼입니다.
// base() 멤버를 통해 내부 receiver를 지정하면,
// 오버라이드하지 않은 메서드는 자동으로 base()로 forwarding됩니다.
///////////////////////////////////////////////////////////////////////////////

// 값을 두 배로 만들고 downstream으로 전달하는 receiver
template <class BaseReceiver>
struct doubling_receiver : exec::receiver_adaptor<doubling_receiver<BaseReceiver>>
{
  BaseReceiver base_;

  explicit doubling_receiver(BaseReceiver r)
    : base_(static_cast<BaseReceiver&&>(r))
  {}

  // receiver_adaptor가 base()를 통해 forwarding에 사용할 receiver를 얻음
  auto base() && noexcept -> BaseReceiver&&
  {
    return static_cast<BaseReceiver&&>(base_);
  }

  auto base() const & noexcept -> BaseReceiver const &
  {
    return base_;
  }

  // set_value만 오버라이드: 값을 두 배로 만들어 forwarding
  void set_value(int x) && noexcept
  {
    // 두 배로 만들어서 base receiver에 전달
    ex::set_value(static_cast<BaseReceiver&&>(base_), x * 2);
  }
  // set_error, set_stopped, get_env 등은 receiver_adaptor가 base()로 자동 forwarding
};

static void example_receiver_adaptor()
{
  section("3. exec::receiver_adaptor CRTP");

  // 수동으로 connect + start
  int result = -1;

  // 간단한 collecting receiver
  struct [[maybe_unused]] collect_receiver
  {
    using receiver_concept [[maybe_unused]] = ex::receiver_t;

    int* result_;

    void set_value(int x) && noexcept
    {
      *result_ = x;
    }

    void set_error(std::error_code) && noexcept {}

    void set_stopped() && noexcept {}
  };

  // doubling_receiver로 래핑
  auto inner_rcvr = collect_receiver{&result};
  auto dbl_rcvr   = doubling_receiver<collect_receiver>{std::move(inner_rcvr)};

  // just(21)과 connect
  auto op = ex::connect(ex::just(21), std::move(dbl_rcvr));
  ex::start(op);

  std::printf("  just(21) -> doubling_receiver -> %d\n", result);  // 42
}

///////////////////////////////////////////////////////////////////////////////
// 4. Retry Sender - 에러 시 자동 재시도 알고리즘
//
// retry(sender)는 sender가 에러로 완료될 때 자동으로 재시작합니다.
// exec::receiver_adaptor를 활용해 set_error만 오버라이드,
// operation state 안에 optional로 중첩 operation을 재구성합니다.
//
// 이 패턴은 examples/algorithms/retry.hpp에서 가져온 것입니다.
///////////////////////////////////////////////////////////////////////////////

// 내부 구현 타입들 (namespace 분리)
namespace _retry_impl
{
  template <class S, class R>
  struct op;

  // set_error만 오버라이드: 에러 시 재시도 트리거
  template <class S, class R>
  struct retry_receiver : exec::receiver_adaptor<retry_receiver<S, R>>
  {
    op<S, R>* op_;

    auto base() && noexcept -> R&&
    {
      return static_cast<R&&>(op_->downstream_rcvr_);
    }

    auto base() const & noexcept -> R const &
    {
      return op_->downstream_rcvr_;
    }

    explicit retry_receiver(op<S, R>* o)
      : op_(o)
    {}

    template <class Error>
    void set_error(Error&&) && noexcept
    {
      // 에러 무시하고 재시도
      op_->retry();
    }
  };

  // _conv: non-movable 타입을 optional에 emplace하기 위한 헬퍼
  template <std::invocable F>
    requires std::is_nothrow_move_constructible_v<F>
  struct conv
  {
    F f_;

    explicit conv(F f) noexcept
      : f_(static_cast<F&&>(f))
    {}

    operator std::invoke_result_t<F>() &&
    {
      return static_cast<F&&>(f_)();
    }
  };

  template <class S, class R>
  struct op
  {
    S                                                             src_;
    R                                                             downstream_rcvr_;
    std::optional<ex::connect_result_t<S&, retry_receiver<S, R>>> inner_op_;

    op(S s, R r)
      : src_(static_cast<S&&>(s))
      , downstream_rcvr_(static_cast<R&&>(r))
      , inner_op_{make_connection()}
    {}

    op(op&&) = delete;  // operation state는 이동 불가

    auto make_connection() noexcept
    {
      return conv{[this] { return ex::connect(src_, retry_receiver<S, R>{this}); }};
    }

    void retry() noexcept
    {
      STDEXEC_TRY
      {
        inner_op_.emplace(make_connection());
        ex::start(*inner_op_);
      }
      STDEXEC_CATCH_ALL
      {
        ex::set_error(static_cast<R&&>(downstream_rcvr_), std::current_exception());
      }
    }

    void start() & noexcept
    {
      ex::start(*inner_op_);
    }
  };
}  // namespace _retry_impl

// retry sender
template <ex::sender S>
struct retry_sender
{
  using sender_concept = ex::sender_t;

  S src_;

  explicit retry_sender(S s)
    : src_(static_cast<S&&>(s))
  {}

  // completion_signatures: 에러 채널 제거 (retry가 에러를 처리), exception_ptr 추가
  template <class>
  using _no_error = ex::completion_signatures<>;
  template <class... Ts>
  using _values = ex::completion_signatures<ex::set_value_t(Ts...)>;

  template <class Self, class... Env>
  static consteval auto get_completion_signatures() -> ex::__transform_completion_signatures_t<
    ex::completion_signatures_of_t<S&, Env...>,
    ex::completion_signatures<ex::set_error_t(std::exception_ptr)>,
    _values,
    _no_error>
  {
    return {};
  }

  template <ex::receiver R>
  auto connect(R r) && -> _retry_impl::op<S, R>
  {
    return {static_cast<S&&>(src_), static_cast<R&&>(r)};
  }
};

template <ex::sender S>
auto retry(S s) -> ex::sender auto
{
  return retry_sender{static_cast<S&&>(s)};
}

// 3번째 시도에서 성공하는 sender
struct flaky3
{
  using sender_concept = ex::sender_t;
  using completion_signatures =
    ex::completion_signatures<ex::set_value_t(int), ex::set_error_t(std::exception_ptr)>;

  template <class R>
  struct op
  {
    R rcvr_;

    void start() & noexcept
    {
      static int i = 0;
      if (++i < 3)
      {
        std::printf("  flaky3: fail (attempt %d)\n", i);
        ex::set_error(static_cast<R&&>(rcvr_), std::exception_ptr{});
      }
      else
      {
        std::printf("  flaky3: success (attempt %d)\n", i);
        ex::set_value(static_cast<R&&>(rcvr_), i * 10);
      }
    }
  };

  template <class R>
  auto connect(R r) const -> op<R>
  {
    return {static_cast<R&&>(r)};
  }
};

static void example_retry_sender()
{
  section("4. Retry Sender 패턴");

  // retry로 감싸면 에러 시 자동 재시도
  auto [val] = ex::sync_wait(retry(flaky3{})).value();
  std::printf("  retry(flaky3) => %d\n", val);  // 30 (3번째 시도: 3*10)
}

///////////////////////////////////////////////////////////////////////////////
// main
///////////////////////////////////////////////////////////////////////////////
auto main() -> int
{
  std::printf("stdexec Playground - 커스텀 Sender/Receiver\n");
  std::printf("=============================================\n");

  example_minimal_sender();
  example_multi_channel_sender();
  example_receiver_adaptor();
  example_retry_sender();

  std::printf("\n=============================================\n");
  std::printf("Done!\n");
  return 0;
}
