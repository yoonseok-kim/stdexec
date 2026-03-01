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
// Example 03: let_value() - Dynamic Sender Creation
//
// Learning objectives:
// - Understand the difference between then() and let_value()
// - Create senders dynamically based on previous results
// - Build conditional and branching workflows
//
// Key concepts:
// - let_value(): Like then(), but the function returns a SENDER (not a value)
// - Dynamic composition: Create different senders based on runtime values
// - then() returns a value, let_value() returns a sender
///////////////////////////////////////////////////////////////////////////////

#include <stdexec/execution.hpp>

#include <cstdio>
#include <string>

namespace ex = stdexec;

auto main() -> int {
  std::printf("=== Example 03: let_value() - Dynamic Sender Creation ===\n\n");

  //////////////////////////////////////////////////////////////////////////////
  // Part 1: Basic difference - then() vs let_value()
  //////////////////////////////////////////////////////////////////////////////
  std::printf("--- Part 1: then() vs let_value() ---\n");
  {
    // With then(): the function returns a VALUE
    // then()의 경우: 함수가 VALUE를 반환합니다
    auto with_then =
      ex::just(10)
      | ex::then([](int x) {
          return x * 2;  // Returns int (a value)
        });

    // With let_value(): the function returns a SENDER
    // let_value()의 경우: 함수가 SENDER를 반환합니다
    auto with_let_value =
      ex::just(10)
      | ex::let_value([](int x) {
          return ex::just(x * 2);  // Returns a sender that produces int
        });

    // Both produce the same result, but let_value allows more flexibility
    // 둘 다 같은 결과를 생성하지만, let_value가 더 유연합니다

    auto result1 = ex::sync_wait(std::move(with_then));
    auto result2 = ex::sync_wait(std::move(with_let_value));

    if (result1 && result2) {
      auto [v1] = *result1;
      auto [v2] = *result2;
      std::printf("then() result: %d\n", v1);
      std::printf("let_value() result: %d\n", v2);
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  // Part 2: Conditional sender creation
  //////////////////////////////////////////////////////////////////////////////
  std::printf("\n--- Part 2: Conditional branching ---\n");
  {
    auto process = [](int value) {
      return ex::just(value)
        | ex::let_value([](int x) {
            // Create different senders based on the value
            // 값에 따라 다른 sender를 생성합니다
            if (x > 0) {
              std::printf("  Value is positive, adding 100\n");
              return ex::just(x + 100);
            } else {
              std::printf("  Value is non-positive, using absolute value\n");
              return ex::just(-x);
            }
          });
    };

    auto result1 = ex::sync_wait(process(42));
    auto result2 = ex::sync_wait(process(-5));

    if (result1 && result2) {
      auto [v1] = *result1;
      auto [v2] = *result2;
      std::printf("process(42) = %d\n", v1);
      std::printf("process(-5) = %d\n", v2);
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  // Part 3: Chaining multiple async operations
  //////////////////////////////////////////////////////////////////////////////
  std::printf("\n--- Part 3: Chaining async operations ---\n");
  {
    // Simulating a chain of async operations where each step
    // depends on the previous step's result
    // 각 단계가 이전 단계의 결과에 의존하는
    // 비동기 작업 체인을 시뮬레이션합니다

    auto async_fetch_user_id = []() {
      std::printf("  Fetching user ID...\n");
      return ex::just(42);  // Simulated user ID
    };

    auto async_fetch_user_name = [](int user_id) {
      std::printf("  Fetching name for user %d...\n", user_id);
      return ex::just(std::string("Alice"));  // Simulated name
    };

    auto async_fetch_greeting = [](std::string name) {
      std::printf("  Generating greeting for %s...\n", name.c_str());
      return ex::just(std::string("Hello, ") + name + "!");
    };

    // Chain all operations together using let_value
    // let_value를 사용하여 모든 작업을 연결합니다
    auto pipeline =
      async_fetch_user_id()
      | ex::let_value([&](int user_id) {
          return async_fetch_user_name(user_id);
        })
      | ex::let_value([&](std::string name) {
          return async_fetch_greeting(std::move(name));
        });

    auto result = ex::sync_wait(std::move(pipeline));
    if (result) {
      auto [greeting] = *result;
      std::printf("Final greeting: %s\n", greeting.c_str());
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  // Part 4: Mixing then() and let_value()
  //////////////////////////////////////////////////////////////////////////////
  std::printf("\n--- Part 4: Mixing then() and let_value() ---\n");
  {
    // Use then() for simple transformations, let_value() when you need
    // to create new senders
    // 단순 변환에는 then()을, 새 sender를 생성해야 할 때는 let_value()를 사용하세요

    auto pipeline =
      ex::just(5)
      | ex::then([](int x) {
          std::printf("  then: multiply by 10 -> %d\n", x * 10);
          return x * 10;  // Simple transformation
        })
      | ex::let_value([](int x) {
          std::printf("  let_value: creating sender for value %d\n", x);
          // Here we can do something more complex that returns a sender
          // 여기서 sender를 반환하는 더 복잡한 작업을 할 수 있습니다
          return ex::just(x, x + 1, x + 2);  // Return multiple values
        })
      | ex::then([](int a, int b, int c) {
          std::printf("  then: received %d, %d, %d\n", a, b, c);
          return a + b + c;
        });

    auto result = ex::sync_wait(std::move(pipeline));
    if (result) {
      auto [sum] = *result;
      std::printf("Final sum: %d\n", sum);
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  // Part 5: Nested let_value for complex workflows
  //////////////////////////////////////////////////////////////////////////////
  std::printf("\n--- Part 5: Nested workflows ---\n");
  {
    // Sometimes you need nested let_value for complex dependencies
    // 복잡한 의존성을 위해 중첩된 let_value가 필요할 때가 있습니다

    auto pipeline =
      ex::just(10)
      | ex::let_value([](int outer) {
          std::printf("  Outer value: %d\n", outer);

          return ex::just(outer * 2)
            | ex::let_value([outer](int inner) {
                // We can capture 'outer' and use both values
                // 'outer'를 캡처하여 두 값을 모두 사용할 수 있습니다
                std::printf("  Inner value: %d (derived from %d)\n", inner, outer);
                return ex::just(outer + inner);
              });
        });

    auto result = ex::sync_wait(std::move(pipeline));
    if (result) {
      auto [value] = *result;
      std::printf("Final: 10 + 20 = %d\n", value);
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  // Part 6: let_value with void sender
  //////////////////////////////////////////////////////////////////////////////
  std::printf("\n--- Part 6: let_value with void sender ---\n");
  {
    // let_value() can also work with senders that produce no values
    // let_value()는 값을 생성하지 않는 sender와도 작동합니다

    auto pipeline =
      ex::just()  // Void sender
      | ex::let_value([]() {
          std::printf("  Starting from void, creating new sender\n");
          return ex::just(42);
        });

    auto result = ex::sync_wait(std::move(pipeline));
    if (result) {
      auto [value] = *result;
      std::printf("Produced value: %d\n", value);
    }
  }

  std::printf("\n=== End of Example 03 ===\n");
  return 0;
}
