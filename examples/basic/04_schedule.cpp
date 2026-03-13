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
// Example 04: schedule() and Schedulers - Execution Contexts
//
// Learning objectives:
// - Understand what a scheduler is: a handle to an execution context
// - Use schedule() to create a sender that starts work on a scheduler
// - Learn about different scheduler types (inline, thread pool)
//
// Key concepts:
// - Scheduler: An object representing an execution context (thread pool, etc.)
// - schedule(): Creates a void sender that completes on the scheduler's context
// - Execution context: Where the work actually runs (main thread, pool, etc.)
///////////////////////////////////////////////////////////////////////////////

#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>

#include <cstdio>
#include <thread>

namespace ex = stdexec;

auto main() -> int {
  std::printf("=== Example 04: schedule() and Schedulers ===\n\n");

  //////////////////////////////////////////////////////////////////////////////
  // Part 1: inline_scheduler - executes on the current thread
  //////////////////////////////////////////////////////////////////////////////
  std::printf("--- Part 1: inline_scheduler ---\n");
  {
    // inline_scheduler executes work immediately on the current thread
    // It's useful for testing and as a default scheduler
    // inline_scheduler는 현재 스레드에서 즉시 작업을 실행합니다
    // 테스트용이나 기본 스케줄러로 유용합니다

    ex::scheduler auto sched = ex::inline_scheduler{};

    // schedule() returns a sender that, when started, completes
    // on the scheduler's execution context (inline = current thread)
    // schedule()은 시작되면 스케줄러의 실행 컨텍스트에서
    // 완료되는 sender를 반환합니다 (inline = 현재 스레드)
    auto snd = ex::schedule(sched)
      | ex::then([]() {
          std::printf("  Running on inline_scheduler (current thread)\n");
          return 42;
        });

    auto result = ex::sync_wait(std::move(snd));
    if (result) {
      auto [value] = *result;
      std::printf("Result: %d\n", value);
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  // Part 2: static_thread_pool - executes on worker threads
  //////////////////////////////////////////////////////////////////////////////
  std::printf("\n--- Part 2: static_thread_pool ---\n");
  {
    // Create a thread pool with 4 worker threads
    // 4개의 워커 스레드를 가진 스레드 풀을 생성합니다
    exec::static_thread_pool pool{4};

    // Get a scheduler from the thread pool
    // 스레드 풀에서 스케줄러를 얻습니다
    ex::scheduler auto sched = pool.get_scheduler();

    // schedule() on the pool scheduler starts work on a pool thread
    // 풀 스케줄러의 schedule()은 풀 스레드에서 작업을 시작합니다
    auto snd = ex::schedule(sched)
      | ex::then([]() {
          std::printf("  Running on a worker thread!\n");
          return 100;
        });

    auto result = ex::sync_wait(std::move(snd));
    if (result) {
      auto [value] = *result;
      std::printf("Result: %d (back on main thread after sync_wait)\n", value);
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  // Part 3: Chaining work after schedule()
  //////////////////////////////////////////////////////////////////////////////
  std::printf("\n--- Part 3: Chaining work after schedule() ---\n");
  {
    exec::static_thread_pool pool{2};
    auto sched = pool.get_scheduler();

    // All work chained after schedule() runs on the pool
    // schedule() 이후에 연결된 모든 작업은 풀에서 실행됩니다
    auto pipeline = ex::schedule(sched)
      | ex::then([]() {
          std::printf("  Step 1: Starting pipeline\n");
          return 10;
        })
      | ex::then([](int x) {
          std::printf("  Step 2: Received %d, multiplying by 2\n", x);
          return x * 2;
        })
      | ex::then([](int x) {
          std::printf("  Step 3: Received %d, adding 5\n", x);
          return x + 5;
        });

    auto result = ex::sync_wait(std::move(pipeline));
    if (result) {
      auto [value] = *result;
      std::printf("Final result: %d\n", value);
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  // Part 4: Using transfer_just() - shorthand for schedule + just
  //////////////////////////////////////////////////////////////////////////////
  std::printf("\n--- Part 4: transfer_just() shorthand ---\n");
  {
    exec::static_thread_pool pool{2};
    auto sched = pool.get_scheduler();

    // transfer_just(sched, values...) is equivalent to:
    // schedule(sched) | then([=]() { return values...; })
    // But more efficient!
    //
    // transfer_just(sched, values...)는 다음과 동일합니다:
    // schedule(sched) | then([=]() { return values...; })
    // 하지만 더 효율적입니다!

    auto snd = ex::transfer_just(sched, 42, 3.14)
      | ex::then([](int i, double d) {
          std::printf("  Received values: %d, %.2f\n", i, d);
          return i;
        });

    auto result = ex::sync_wait(std::move(snd));
    if (result) {
      auto [value] = *result;
      std::printf("Result: %d\n", value);
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  // Part 5: schedule() returns a void sender
  //////////////////////////////////////////////////////////////////////////////
  std::printf("\n--- Part 5: schedule() produces no values ---\n");
  {
    exec::static_thread_pool pool{2};
    auto sched = pool.get_scheduler();

    // schedule() itself doesn't produce any values
    // It just transfers execution to the scheduler's context
    // schedule() 자체는 어떤 값도 생성하지 않습니다
    // 단지 실행을 스케줄러의 컨텍스트로 전환할 뿐입니다

    auto snd = ex::schedule(sched)
      | ex::then([]() {  // Note: no parameters - schedule produces nothing
          std::printf("  schedule() completed, now running on pool\n");
          return 99;
        });

    auto result = ex::sync_wait(std::move(snd));
    if (result) {
      auto [value] = *result;
      std::printf("Result: %d\n", value);
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  // Part 6: Multiple independent tasks on the same pool
  //////////////////////////////////////////////////////////////////////////////
  std::printf("\n--- Part 6: Multiple tasks on same pool ---\n");
  {
    exec::static_thread_pool pool{4};
    auto sched = pool.get_scheduler();

    // Create multiple senders that will run on the pool
    // 풀에서 실행될 여러 sender를 생성합니다
    auto task1 = ex::schedule(sched)
      | ex::then([]() {
          std::printf("  Task 1 running\n");
          return 1;
        });

    auto task2 = ex::schedule(sched)
      | ex::then([]() {
          std::printf("  Task 2 running\n");
          return 2;
        });

    auto task3 = ex::schedule(sched)
      | ex::then([]() {
          std::printf("  Task 3 running\n");
          return 3;
        });

    // Execute all tasks and combine results with when_all
    // (when_all is covered in detail in example 05)
    // 모든 태스크를 실행하고 when_all로 결과를 결합합니다
    // (when_all은 예제 05에서 자세히 다룹니다)
    auto combined = ex::when_all(
      std::move(task1),
      std::move(task2),
      std::move(task3)
    );

    auto result = ex::sync_wait(std::move(combined));
    if (result) {
      auto [a, b, c] = *result;
      std::printf("Combined results: %d, %d, %d (sum: %d)\n", a, b, c, a + b + c);
    }
  }

  std::printf("\n=== End of Example 04 ===\n");
  return 0;
}
