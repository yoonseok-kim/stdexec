/*
 * Copyright (c) 2024 NVIDIA Corporation
 *
 * Licensed under the Apache License Version 2.0 with LLVM Exceptions
 * (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 *   https://llvm.org/LICENSE.txt
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

///////////////////////////////////////////////////////////////////////////////
// Example 01: just() and sync_wait() - The Foundation of Senders
//
// Learning objectives:
// - Understand what a "sender" is: an object describing asynchronous work
// - Learn to create a sender that produces values using just()
// - Learn to execute a sender and wait for its result using sync_wait()
//
// Key concepts:
// - Sender: A description of work that will produce values (lazy evaluation)
// - just(): Creates a sender that immediately completes with given values
// - sync_wait(): Blocks the current thread until the sender completes
///////////////////////////////////////////////////////////////////////////////

#include <stdexec/execution.hpp>

#include <cstdio>
#include <string>
#include <tuple>

// Namespace alias for convenience
namespace ex = stdexec;

auto main() -> int {
  std::printf("=== Example 01: just() and sync_wait() ===\n\n");

  //////////////////////////////////////////////////////////////////////////////
  // Part 1: The simplest sender - just() with a single value
  //////////////////////////////////////////////////////////////////////////////
  std::printf("--- Part 1: Single value ---\n");
  {
    // just(42) creates a "sender" that, when started, immediately completes
    // with the value 42. Note: No work happens here yet! This is "lazy".
    // just(42)는 시작되면 즉시 값 42로 완료되는 "sender"를 생성합니다.
    // 주의: 여기서는 아직 아무 작업도 일어나지 않습니다! 이것이 "lazy" 평가입니다.
    ex::sender auto snd = ex::just(42);

    // sync_wait() actually executes the sender and blocks until completion.
    // It returns std::optional<std::tuple<...>> containing the result values.
    // sync_wait()는 실제로 sender를 실행하고 완료될 때까지 블록합니다.
    // 결과 값을 포함하는 std::optional<std::tuple<...>>을 반환합니다.
    std::optional result = ex::sync_wait(std::move(snd));

    // Extract the value from the result tuple
    // 결과 튜플에서 값을 추출합니다
    if (result) {
      auto [value] = *result;  // Structured binding to extract the int
      std::printf("Result: %d\n", value);
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  // Part 2: just() with multiple values
  //////////////////////////////////////////////////////////////////////////////
  std::printf("\n--- Part 2: Multiple values ---\n");
  {
    // just() can produce multiple values at once
    // just()는 한 번에 여러 값을 생성할 수 있습니다
    ex::sender auto snd = ex::just(1, 2.5, std::string("hello"));

    auto result = ex::sync_wait(std::move(snd));
    if (result) {
      // Unpack all three values
      // 세 개의 값을 모두 언팩합니다
      auto [i, d, s] = *result;
      std::printf("int: %d, double: %.1f, string: %s\n", i, d, s.c_str());
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  // Part 3: just() with no values (void sender)
  //////////////////////////////////////////////////////////////////////////////
  std::printf("\n--- Part 3: Void sender (no values) ---\n");
  {
    // just() with no arguments creates a sender that completes with no values
    // This is useful for signaling completion of side-effect-only operations
    // 인자 없는 just()는 값 없이 완료되는 sender를 생성합니다
    // 부수 효과만 있는 작업의 완료를 알리는 데 유용합니다
    ex::sender auto snd = ex::just();

    auto result = ex::sync_wait(std::move(snd));
    if (result) {
      std::printf("Void sender completed successfully!\n");
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  // Part 4: Understanding laziness - sender is just a description
  //////////////////////////////////////////////////////////////////////////////
  std::printf("\n--- Part 4: Demonstrating laziness ---\n");
  {
    std::printf("Creating sender...\n");

    // This lambda will be called when the sender is executed, not now
    // 이 람다는 sender가 실행될 때 호출되며, 지금이 아닙니다
    auto make_value = [] {
      std::printf("  [Inside lambda: Computing value...]\n");
      return 100;
    };

    // Note: The lambda is captured but NOT called yet
    // 주의: 람다가 캡처되지만 아직 호출되지 않습니다
    ex::sender auto snd = ex::just(make_value());

    // Actually, just() evaluates its arguments immediately.
    // To truly defer computation, we need then() (covered in example 02)
    // 실제로 just()는 인자를 즉시 평가합니다.
    // 진정한 지연 계산을 위해서는 then()이 필요합니다 (예제 02에서 다룸)

    std::printf("About to execute sender...\n");
    auto result = ex::sync_wait(std::move(snd));
    if (result) {
      auto [value] = *result;
      std::printf("Final result: %d\n", value);
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  // Part 5: Error handling - sync_wait returns optional
  //////////////////////////////////////////////////////////////////////////////
  std::printf("\n--- Part 5: sync_wait() returns optional ---\n");
  {
    // sync_wait() returns std::optional because:
    // 1. The sender might complete with "stopped" (cancellation)
    // 2. This allows checking if work completed successfully
    //
    // sync_wait()가 std::optional을 반환하는 이유:
    // 1. sender가 "stopped" (취소)로 완료될 수 있습니다
    // 2. 작업이 성공적으로 완료되었는지 확인할 수 있습니다

    ex::sender auto snd = ex::just(999);
    auto result = ex::sync_wait(std::move(snd));

    // Always check if the result has a value
    // 항상 결과에 값이 있는지 확인하세요
    if (result.has_value()) {
      auto [value] = result.value();
      std::printf("Success! Value: %d\n", value);
    } else {
      std::printf("Sender was stopped (cancelled)\n");
    }
  }

  std::printf("\n=== End of Example 01 ===\n");
  return 0;
}
