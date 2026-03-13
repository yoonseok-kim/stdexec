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
// Example 02: then() - Chaining Operations on Senders
//
// Learning objectives:
// - Transform sender results using then()
// - Chain multiple operations using the pipe operator (|)
// - Understand how then() differs from direct function calls
//
// Key concepts:
// - then(): Attaches a continuation that transforms the sender's result
// - Pipe operator (|): Fluent syntax for chaining sender operations
// - Composition: Building complex workflows from simple operations
///////////////////////////////////////////////////////////////////////////////

#include <stdexec/execution.hpp>

#include <cstdio>
#include <string>

namespace ex = stdexec;

auto main() -> int {
  std::printf("=== Example 02: then() - Chaining Operations ===\n\n");

  //////////////////////////////////////////////////////////////////////////////
  // Part 1: Basic then() - transforming a value
  //////////////////////////////////////////////////////////////////////////////
  std::printf("--- Part 1: Basic transformation ---\n");
  {
    // Create a sender that produces 10
    // 10을 생성하는 sender를 만듭니다
    ex::sender auto snd1 = ex::just(10);

    // then() takes the output of snd1 and applies a function to it
    // The result is a NEW sender that produces the transformed value
    // then()은 snd1의 출력을 받아 함수를 적용합니다
    // 결과는 변환된 값을 생성하는 새로운 sender입니다
    ex::sender auto snd2 = ex::then(std::move(snd1), [](int x) {
      std::printf("  Received: %d, transforming...\n", x);
      return x * 2;  // Transform: multiply by 2
    });

    auto result = ex::sync_wait(std::move(snd2));
    if (result) {
      auto [value] = *result;
      std::printf("Final result: %d\n", value);
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  // Part 2: Pipe operator - fluent chaining syntax
  //////////////////////////////////////////////////////////////////////////////
  std::printf("\n--- Part 2: Pipe operator syntax ---\n");
  {
    // The pipe operator (|) provides a more readable way to chain operations
    // sender | then(f) is equivalent to then(sender, f)
    // 파이프 연산자(|)는 연산을 연결하는 더 읽기 쉬운 방법을 제공합니다
    // sender | then(f)는 then(sender, f)와 동일합니다

    auto result = ex::sync_wait(
      ex::just(5)
      | ex::then([](int x) { return x + 10; })  // 5 -> 15
      | ex::then([](int x) { return x * 2; })   // 15 -> 30
    );

    if (result) {
      auto [value] = *result;
      std::printf("5 + 10 = 15, 15 * 2 = %d\n", value);
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  // Part 3: Multi-step pipeline
  //////////////////////////////////////////////////////////////////////////////
  std::printf("\n--- Part 3: Multi-step pipeline ---\n");
  {
    // Build a processing pipeline with multiple steps
    // 여러 단계로 구성된 처리 파이프라인을 구축합니다

    auto pipeline =
      ex::just(100)
      | ex::then([](int x) {
          std::printf("  Step 1: Start with %d\n", x);
          return x;
        })
      | ex::then([](int x) {
          std::printf("  Step 2: Add 50 -> %d\n", x + 50);
          return x + 50;
        })
      | ex::then([](int x) {
          std::printf("  Step 3: Divide by 2 -> %d\n", x / 2);
          return x / 2;
        })
      | ex::then([](int x) {
          std::printf("  Step 4: Convert to string\n");
          return std::string("Result: ") + std::to_string(x);
        });

    auto result = ex::sync_wait(std::move(pipeline));
    if (result) {
      auto [str] = *result;
      std::printf("Final: %s\n", str.c_str());
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  // Part 4: Changing types through the pipeline
  //////////////////////////////////////////////////////////////////////////////
  std::printf("\n--- Part 4: Type transformations ---\n");
  {
    // then() can change the type of the value flowing through
    // then()은 흐르는 값의 타입을 변경할 수 있습니다

    auto pipeline =
      ex::just(42)                                    // int
      | ex::then([](int x) {
          return static_cast<double>(x) / 10.0;       // int -> double
        })
      | ex::then([](double x) {
          return std::string("Value: ") + std::to_string(x);  // double -> string
        })
      | ex::then([](std::string s) {
          return s.length();                          // string -> size_t
        });

    auto result = ex::sync_wait(std::move(pipeline));
    if (result) {
      auto [len] = *result;
      std::printf("String length: %zu\n", len);
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  // Part 5: then() with multiple input values
  //////////////////////////////////////////////////////////////////////////////
  std::printf("\n--- Part 5: Multiple input values ---\n");
  {
    // When a sender produces multiple values, then() receives all of them
    // sender가 여러 값을 생성하면 then()은 그 모든 값을 받습니다

    auto pipeline =
      ex::just(10, 20, 30)  // Produces three integers
      | ex::then([](int a, int b, int c) {
          std::printf("  Received: %d, %d, %d\n", a, b, c);
          return a + b + c;  // Combine into single value
        });

    auto result = ex::sync_wait(std::move(pipeline));
    if (result) {
      auto [sum] = *result;
      std::printf("Sum: %d\n", sum);
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  // Part 6: then() with void sender
  //////////////////////////////////////////////////////////////////////////////
  std::printf("\n--- Part 6: then() with void sender ---\n");
  {
    // then() can also work with senders that produce no values
    // then()은 값을 생성하지 않는 sender와도 작동합니다

    auto pipeline =
      ex::just()  // Void sender (no values)
      | ex::then([]() {
          std::printf("  Continuation called with no arguments\n");
          return 42;  // Now produce a value
        });

    auto result = ex::sync_wait(std::move(pipeline));
    if (result) {
      auto [value] = *result;
      std::printf("Produced value: %d\n", value);
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  // Part 7: Returning void from then()
  //////////////////////////////////////////////////////////////////////////////
  std::printf("\n--- Part 7: Returning void from then() ---\n");
  {
    // The continuation can also return void (for side effects only)
    // 연속은 void를 반환할 수도 있습니다 (부수 효과만 있는 경우)

    auto pipeline =
      ex::just(42)
      | ex::then([](int x) {
          std::printf("  Processing value: %d\n", x);
          // No return statement - this is a void-returning continuation
          // return 문 없음 - void를 반환하는 연속입니다
        });

    auto result = ex::sync_wait(std::move(pipeline));
    if (result) {
      std::printf("Pipeline completed (void result)\n");
    }
  }

  std::printf("\n=== End of Example 02 ===\n");
  return 0;
}
