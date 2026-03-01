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
// Example 06: starts_on() and continues_on() - Execution Context Transitions
//
// Learning objectives:
// - Use starts_on() to run a sender on a specific scheduler
// - Use continues_on() to transition to a different scheduler mid-pipeline
// - Understand the difference between starts_on and continues_on
//
// Key concepts:
// - starts_on(scheduler, sender): Runs the sender starting on the scheduler
// - continues_on(scheduler): Transitions subsequent work to run on scheduler
// - Context switching: Moving work between execution contexts (thread pools, etc.)
///////////////////////////////////////////////////////////////////////////////

#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>

#include <cstdio>

namespace ex = stdexec;

auto main() -> int {
  std::printf("=== Example 06: starts_on() and continues_on() ===\n\n");

  //////////////////////////////////////////////////////////////////////////////
  // Part 1: starts_on() - run entire sender on a scheduler
  //////////////////////////////////////////////////////////////////////////////
  std::printf("--- Part 1: starts_on() basics ---\n");
  {
    exec::static_thread_pool pool{2};
    auto sched = pool.get_scheduler();

    // starts_on() takes a scheduler and a sender, and returns a new sender
    // that runs the entire original sender on that scheduler
    //
    // starts_on()은 스케줄러와 sender를 받아, 원래 sender 전체를
    // 해당 스케줄러에서 실행하는 새 sender를 반환합니다

    auto work = ex::just(42)
      | ex::then([](int x) {
          std::printf("  Processing %d on thread pool\n", x);
          return x * 2;
        });

    // Run 'work' on the thread pool scheduler
    // 'work'를 스레드 풀 스케줄러에서 실행
    auto scheduled_work = ex::starts_on(sched, std::move(work));

    auto result = ex::sync_wait(std::move(scheduled_work));
    if (result) {
      auto [value] = *result;
      std::printf("Result: %d\n", value);
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  // Part 2: continues_on() - transition mid-pipeline
  //////////////////////////////////////////////////////////////////////////////
  std::printf("\n--- Part 2: continues_on() for context transition ---\n");
  {
    exec::static_thread_pool pool1{2};
    exec::static_thread_pool pool2{2};
    auto sched1 = pool1.get_scheduler();
    auto sched2 = pool2.get_scheduler();

    // continues_on() transitions subsequent operations to a different scheduler
    // continues_on()은 이후 작업을 다른 스케줄러로 전환합니다

    auto pipeline = ex::schedule(sched1)
      | ex::then([]() {
          std::printf("  Step 1: Running on Pool 1\n");
          return 10;
        })
      | ex::continues_on(sched2)  // Switch to pool2 for the rest
      | ex::then([](int x) {
          std::printf("  Step 2: Running on Pool 2 (after continues_on)\n");
          return x + 5;
        })
      | ex::then([](int x) {
          std::printf("  Step 3: Still on Pool 2\n");
          return x * 2;
        });

    auto result = ex::sync_wait(std::move(pipeline));
    if (result) {
      auto [value] = *result;
      std::printf("Final result: %d\n", value);
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  // Part 3: Multiple context switches
  //////////////////////////////////////////////////////////////////////////////
  std::printf("\n--- Part 3: Multiple context switches ---\n");
  {
    exec::static_thread_pool cpu_pool{4};
    auto cpu_sched = cpu_pool.get_scheduler();

    // Inline scheduler for lightweight operations
    auto inline_sched = ex::inline_scheduler{};

    // You can switch contexts multiple times in a pipeline
    // 파이프라인에서 여러 번 컨텍스트를 전환할 수 있습니다

    auto pipeline = ex::just(100)
      | ex::continues_on(cpu_sched)
      | ex::then([](int x) {
          std::printf("  [CPU Pool] Heavy computation: %d\n", x);
          return x + 50;
        })
      | ex::continues_on(inline_sched)
      | ex::then([](int x) {
          std::printf("  [Inline] Quick processing: %d\n", x);
          return x * 2;
        })
      | ex::continues_on(cpu_sched)
      | ex::then([](int x) {
          std::printf("  [CPU Pool] More heavy work: %d\n", x);
          return x - 10;
        });

    auto result = ex::sync_wait(std::move(pipeline));
    if (result) {
      auto [value] = *result;
      std::printf("Result: %d\n", value);
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  // Part 4: starts_on vs continues_on comparison
  //////////////////////////////////////////////////////////////////////////////
  std::printf("\n--- Part 4: starts_on vs continues_on ---\n");
  {
    exec::static_thread_pool pool{2};
    auto sched = pool.get_scheduler();

    // starts_on: The ENTIRE sender runs on the scheduler
    // starts_on: 전체 sender가 스케줄러에서 실행됩니다
    std::printf("Using starts_on:\n");
    {
      auto work = ex::just(1)
        | ex::then([](int x) {
            std::printf("  [starts_on] Step 1\n");
            return x + 1;
          })
        | ex::then([](int x) {
            std::printf("  [starts_on] Step 2\n");
            return x + 1;
          });

      auto scheduled = ex::starts_on(sched, std::move(work));
      ex::sync_wait(std::move(scheduled));
    }

    // continues_on: Only operations AFTER it run on the new scheduler
    // continues_on: 그 이후의 작업만 새 스케줄러에서 실행됩니다
    std::printf("Using continues_on:\n");
    {
      auto pipeline = ex::just(1)
        | ex::then([](int x) {
            std::printf("  [continues_on] Step 1 (before transition)\n");
            return x + 1;
          })
        | ex::continues_on(sched)  // Transition point
        | ex::then([](int x) {
            std::printf("  [continues_on] Step 2 (after transition)\n");
            return x + 1;
          });

      ex::sync_wait(std::move(pipeline));
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  // Part 5: Combining with when_all
  //////////////////////////////////////////////////////////////////////////////
  std::printf("\n--- Part 5: Context transitions with when_all ---\n");
  {
    exec::static_thread_pool pool{4};
    auto sched = pool.get_scheduler();

    // Run parallel work on thread pool, then process results inline
    // 스레드 풀에서 병렬 작업을 실행한 후, 결과를 인라인으로 처리

    auto task1 = ex::starts_on(sched, ex::just(10)
      | ex::then([](int x) {
          std::printf("  Task 1 on pool\n");
          return x;
        }));

    auto task2 = ex::starts_on(sched, ex::just(20)
      | ex::then([](int x) {
          std::printf("  Task 2 on pool\n");
          return x;
        }));

    auto combined = ex::when_all(std::move(task1), std::move(task2))
      | ex::continues_on(ex::inline_scheduler{})
      | ex::then([](int a, int b) {
          std::printf("  Combining results inline: %d + %d\n", a, b);
          return a + b;
        });

    auto result = ex::sync_wait(std::move(combined));
    if (result) {
      auto [value] = *result;
      std::printf("Combined result: %d\n", value);
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  // Part 6: Practical pattern - CPU-bound work on pool
  //////////////////////////////////////////////////////////////////////////////
  std::printf("\n--- Part 6: Practical pattern ---\n");
  {
    exec::static_thread_pool worker_pool{4};
    auto workers = worker_pool.get_scheduler();

    // Common pattern: Start inline, do heavy work on pool, finish inline
    // 일반적인 패턴: 인라인으로 시작, 무거운 작업은 풀에서, 인라인으로 마무리

    auto pipeline = ex::just(std::string("input data"))
      | ex::then([](std::string data) {
          std::printf("  [Main] Received: %s\n", data.c_str());
          return data;
        })
      | ex::continues_on(workers)
      | ex::then([](std::string data) {
          std::printf("  [Worker] Processing: %s\n", data.c_str());
          // Simulate CPU-intensive work
          return std::string("processed: ") + data;
        })
      | ex::continues_on(ex::inline_scheduler{})
      | ex::then([](std::string result) {
          std::printf("  [Main] Final result: %s\n", result.c_str());
          return result;
        });

    auto result = ex::sync_wait(std::move(pipeline));
    if (result) {
      auto [str] = *result;
      std::printf("Output: %s\n", str.c_str());
    }
  }

  std::printf("\n=== End of Example 06 ===\n");
  return 0;
}
