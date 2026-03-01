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
// Example 05: when_all() - Parallel Execution and Result Combination
//
// Learning objectives:
// - Execute multiple senders concurrently using when_all()
// - Combine results from parallel operations
// - Understand how when_all() handles multiple completion values
//
// Key concepts:
// - when_all(): Starts multiple senders concurrently, completes when ALL finish
// - Parallel execution: Independent senders can run simultaneously
// - Result aggregation: All results are collected into a combined completion
///////////////////////////////////////////////////////////////////////////////

#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>

#include <cstdio>
#include <chrono>
#include <thread>

namespace ex = stdexec;

// Helper to simulate work with a delay
void simulate_work(const char* name, int ms) {
  std::printf("  [%s] Starting (will take %dms)\n", name, ms);
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
  std::printf("  [%s] Done\n", name);
}

auto main() -> int {
  std::printf("=== Example 05: when_all() - Parallel Execution ===\n\n");

  //////////////////////////////////////////////////////////////////////////////
  // Part 1: Basic when_all() - combining two senders
  //////////////////////////////////////////////////////////////////////////////
  std::printf("--- Part 1: Basic when_all() ---\n");
  {
    // when_all() takes multiple senders and returns a sender that:
    // 1. Starts all input senders concurrently
    // 2. Completes when ALL of them complete
    // 3. Produces a tuple of all their results
    //
    // when_all()은 여러 sender를 받아 다음과 같은 sender를 반환합니다:
    // 1. 모든 입력 sender를 동시에 시작
    // 2. 모든 sender가 완료되면 완료
    // 3. 모든 결과의 튜플을 생성

    auto snd1 = ex::just(10);
    auto snd2 = ex::just(20);

    auto combined = ex::when_all(std::move(snd1), std::move(snd2));

    auto result = ex::sync_wait(std::move(combined));
    if (result) {
      auto [a, b] = *result;
      std::printf("Results: %d, %d (sum: %d)\n", a, b, a + b);
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  // Part 2: when_all() with different types
  //////////////////////////////////////////////////////////////////////////////
  std::printf("\n--- Part 2: Different result types ---\n");
  {
    // when_all() can combine senders that produce different types
    // when_all()은 다른 타입을 생성하는 sender들을 결합할 수 있습니다

    auto int_sender = ex::just(42);
    auto double_sender = ex::just(3.14);
    auto string_sender = ex::just(std::string("hello"));

    auto combined = ex::when_all(
      std::move(int_sender),
      std::move(double_sender),
      std::move(string_sender)
    );

    auto result = ex::sync_wait(std::move(combined));
    if (result) {
      auto [i, d, s] = *result;
      std::printf("int: %d, double: %.2f, string: %s\n", i, d, s.c_str());
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  // Part 3: Parallel execution on thread pool
  //////////////////////////////////////////////////////////////////////////////
  std::printf("\n--- Part 3: True parallel execution ---\n");
  {
    exec::static_thread_pool pool{4};
    auto sched = pool.get_scheduler();

    auto start_time = std::chrono::steady_clock::now();

    // Each task simulates work with a delay
    // These will run in PARALLEL on different threads
    // 각 태스크가 지연이 있는 작업을 시뮬레이션합니다
    // 이들은 다른 스레드에서 병렬로 실행됩니다

    auto task1 = ex::schedule(sched)
      | ex::then([]() {
          simulate_work("Task 1", 100);
          return 1;
        });

    auto task2 = ex::schedule(sched)
      | ex::then([]() {
          simulate_work("Task 2", 150);
          return 2;
        });

    auto task3 = ex::schedule(sched)
      | ex::then([]() {
          simulate_work("Task 3", 80);
          return 3;
        });

    // All three tasks start at once!
    // 세 태스크가 동시에 시작됩니다!
    auto combined = ex::when_all(
      std::move(task1),
      std::move(task2),
      std::move(task3)
    );

    auto result = ex::sync_wait(std::move(combined));

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time).count();

    if (result) {
      auto [a, b, c] = *result;
      std::printf("Results: %d, %d, %d\n", a, b, c);
      std::printf("Total time: ~%lldms (parallel, not 330ms sequential)\n", duration);
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  // Part 4: when_all() with void senders
  //////////////////////////////////////////////////////////////////////////////
  std::printf("\n--- Part 4: when_all() with void senders ---\n");
  {
    // Senders that produce no values can also be combined
    // void sender들도 결합할 수 있습니다

    auto action1 = ex::just() | ex::then([]() {
      std::printf("  Action 1 executed\n");
    });

    auto action2 = ex::just() | ex::then([]() {
      std::printf("  Action 2 executed\n");
    });

    auto combined = ex::when_all(std::move(action1), std::move(action2));

    auto result = ex::sync_wait(std::move(combined));
    if (result) {
      std::printf("Both actions completed\n");
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  // Part 5: Chaining after when_all()
  //////////////////////////////////////////////////////////////////////////////
  std::printf("\n--- Part 5: Processing combined results ---\n");
  {
    // Use then() after when_all() to process the combined results
    // when_all() 이후 then()을 사용하여 결합된 결과를 처리합니다

    auto pipeline = ex::when_all(
        ex::just(10),
        ex::just(20),
        ex::just(30)
      )
      | ex::then([](int a, int b, int c) {
          std::printf("  Received: %d, %d, %d\n", a, b, c);
          return a + b + c;  // Combine into single result
        })
      | ex::then([](int sum) {
          std::printf("  Sum: %d\n", sum);
          return sum * 2;
        });

    auto result = ex::sync_wait(std::move(pipeline));
    if (result) {
      auto [value] = *result;
      std::printf("Final result: %d\n", value);
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  // Part 6: when_all() with senders producing multiple values
  //////////////////////////////////////////////////////////////////////////////
  std::printf("\n--- Part 6: Senders with multiple values ---\n");
  {
    // If senders produce multiple values, they are flattened
    // sender가 여러 값을 생성하면 평탄화됩니다

    auto snd1 = ex::just(1, 2);      // Produces two ints
    auto snd2 = ex::just(3.0, 4.0);  // Produces two doubles

    auto combined = ex::when_all(std::move(snd1), std::move(snd2));

    auto result = ex::sync_wait(std::move(combined));
    if (result) {
      // All four values are available
      // 네 개의 값 모두 사용 가능
      auto [a, b, c, d] = *result;
      std::printf("Values: %d, %d, %.1f, %.1f\n", a, b, c, d);
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  // Part 7: Nested when_all()
  //////////////////////////////////////////////////////////////////////////////
  std::printf("\n--- Part 7: Nested parallelism ---\n");
  {
    exec::static_thread_pool pool{4};
    auto sched = pool.get_scheduler();

    // You can nest when_all() for more complex parallel structures
    // 더 복잡한 병렬 구조를 위해 when_all()을 중첩할 수 있습니다

    auto group1 = ex::when_all(
      ex::transfer_just(sched, 1) | ex::then([](int x) {
        std::printf("  Group1-Task1: %d\n", x);
        return x;
      }),
      ex::transfer_just(sched, 2) | ex::then([](int x) {
        std::printf("  Group1-Task2: %d\n", x);
        return x;
      })
    );

    auto group2 = ex::when_all(
      ex::transfer_just(sched, 10) | ex::then([](int x) {
        std::printf("  Group2-Task1: %d\n", x);
        return x;
      }),
      ex::transfer_just(sched, 20) | ex::then([](int x) {
        std::printf("  Group2-Task2: %d\n", x);
        return x;
      })
    );

    // Combine the two groups
    auto all = ex::when_all(std::move(group1), std::move(group2))
      | ex::then([](int a, int b, int c, int d) {
          return a + b + c + d;
        });

    auto result = ex::sync_wait(std::move(all));
    if (result) {
      auto [sum] = *result;
      std::printf("Sum of all tasks: %d (expected: 1+2+10+20=33)\n", sum);
    }
  }

  std::printf("\n=== End of Example 05 ===\n");
  return 0;
}
