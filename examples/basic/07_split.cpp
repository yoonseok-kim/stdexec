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
// Example 07: split() - Sharing a Sender's Result
//
// Learning objectives:
// - Understand that senders are single-use by default
// - Use split() to create a multi-use sender
// - Share computation results across multiple consumers
//
// Key concepts:
// - Single-use: A sender can normally only be connected/started once
// - split(): Creates a sender that can be used multiple times
// - Shared result: The underlying computation runs once, result shared
///////////////////////////////////////////////////////////////////////////////

#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>

#include <cstdio>

namespace ex = stdexec;

auto main() -> int {
  std::printf("=== Example 07: split() - Sharing Sender Results ===\n\n");

  //////////////////////////////////////////////////////////////////////////////
  // Part 1: The problem - senders are single-use
  //////////////////////////////////////////////////////////////////////////////
  std::printf("--- Part 1: Understanding single-use nature ---\n");
  {
    // Normally, a sender can only be used ONCE
    // You cannot do: sync_wait(snd); sync_wait(snd); // Error!
    //
    // 일반적으로 sender는 단 한 번만 사용할 수 있습니다
    // sync_wait(snd); sync_wait(snd); // 오류!

    auto snd = ex::just(42);

    // First use - this consumes the sender
    // 첫 번째 사용 - sender가 소비됩니다
    auto result = ex::sync_wait(std::move(snd));
    if (result) {
      auto [value] = *result;
      std::printf("First use: %d\n", value);
    }

    // Cannot use 'snd' again - it was moved!
    // 'snd'를 다시 사용할 수 없음 - 이동됨!
    // auto result2 = ex::sync_wait(std::move(snd)); // WRONG!

    std::printf("(Cannot reuse moved sender)\n");
  }

  //////////////////////////////////////////////////////////////////////////////
  // Part 2: split() enables multiple uses
  //////////////////////////////////////////////////////////////////////////////
  std::printf("\n--- Part 2: split() for reusability ---\n");
  {
    // split() creates a sender whose result can be observed multiple times
    // The underlying work runs ONCE, but the result is shared
    //
    // split()은 결과를 여러 번 관찰할 수 있는 sender를 생성합니다
    // 기본 작업은 한 번 실행되지만, 결과는 공유됩니다

    auto expensive_computation = ex::just(42)
      | ex::then([](int x) {
          std::printf("  [Expensive computation running...]\n");
          return x * 2;
        });

    // split() the sender to allow multiple uses
    // sender를 split()하여 다중 사용 가능하게 함
    auto shared = ex::split(std::move(expensive_computation));

    // Now we can use 'shared' multiple times!
    // 이제 'shared'를 여러 번 사용할 수 있습니다!

    // First consumer
    auto result1 = ex::sync_wait(shared);
    if (result1) {
      auto [value] = *result1;
      std::printf("First consumer: %d\n", value);
    }

    // Second consumer - computation NOT run again
    auto result2 = ex::sync_wait(shared);
    if (result2) {
      auto [value] = *result2;
      std::printf("Second consumer: %d\n", value);
    }

    // Third consumer
    auto result3 = ex::sync_wait(shared);
    if (result3) {
      auto [value] = *result3;
      std::printf("Third consumer: %d\n", value);
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  // Part 3: Split with parallel consumers
  //////////////////////////////////////////////////////////////////////////////
  std::printf("\n--- Part 3: Multiple parallel consumers ---\n");
  {
    exec::static_thread_pool pool{4};
    auto sched = pool.get_scheduler();

    // Create and split an expensive computation
    // 비용이 큰 계산을 생성하고 split합니다
    auto computation = ex::transfer_just(sched, 100)
      | ex::then([](int x) {
          std::printf("  [Computing base value: %d]\n", x);
          return x;
        });

    auto shared = ex::split(std::move(computation));

    // Multiple parallel consumers can use the shared result
    // 여러 병렬 소비자가 공유된 결과를 사용할 수 있습니다
    auto consumer1 = shared
      | ex::then([](int x) {
          std::printf("  Consumer 1: x + 10 = %d\n", x + 10);
          return x + 10;
        });

    auto consumer2 = shared
      | ex::then([](int x) {
          std::printf("  Consumer 2: x * 2 = %d\n", x * 2);
          return x * 2;
        });

    auto consumer3 = shared
      | ex::then([](int x) {
          std::printf("  Consumer 3: x - 5 = %d\n", x - 5);
          return x - 5;
        });

    // Run all consumers in parallel and collect results
    // 모든 소비자를 병렬로 실행하고 결과 수집
    auto combined = ex::when_all(
      std::move(consumer1),
      std::move(consumer2),
      std::move(consumer3)
    );

    auto result = ex::sync_wait(std::move(combined));
    if (result) {
      auto [r1, r2, r3] = *result;
      std::printf("Results: %d, %d, %d\n", r1, r2, r3);
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  // Part 4: Split preserves the scheduler context
  //////////////////////////////////////////////////////////////////////////////
  std::printf("\n--- Part 4: Split with context ---\n");
  {
    exec::static_thread_pool pool{2};
    auto sched = pool.get_scheduler();

    // The split sender remembers where it completes
    // split된 sender는 완료되는 위치를 기억합니다

    auto work = ex::schedule(sched)
      | ex::then([]() {
          std::printf("  Work running on pool\n");
          return 42;
        });

    auto shared = ex::split(std::move(work));

    // Both consumers will receive the result that was computed on the pool
    // 두 소비자 모두 풀에서 계산된 결과를 받습니다

    auto use1 = shared | ex::then([](int x) {
      std::printf("  Use 1: got %d\n", x);
      return x;
    });

    auto use2 = shared | ex::then([](int x) {
      std::printf("  Use 2: got %d\n", x);
      return x;
    });

    ex::sync_wait(ex::when_all(std::move(use1), std::move(use2)));
  }

  //////////////////////////////////////////////////////////////////////////////
  // Part 5: Practical use case - caching expensive results
  //////////////////////////////////////////////////////////////////////////////
  std::printf("\n--- Part 5: Practical caching pattern ---\n");
  {
    // Imagine fetching data that multiple parts of your program need
    // 프로그램의 여러 부분에서 필요한 데이터를 가져오는 상황을 상상해보세요

    auto fetch_config = ex::just(std::string("config"))
      | ex::then([](std::string name) {
          std::printf("  [Fetching %s...]\n", name.c_str());
          // Simulated expensive fetch
          return std::string("config_value=42");
        });

    // Split so multiple consumers can use the config
    // 여러 소비자가 설정을 사용할 수 있도록 split
    auto shared_config = ex::split(std::move(fetch_config));

    // Different parts of the application use the config
    // 애플리케이션의 다른 부분들이 설정을 사용
    auto module_a = shared_config
      | ex::then([](std::string cfg) {
          std::printf("  Module A using: %s\n", cfg.c_str());
          return 1;
        });

    auto module_b = shared_config
      | ex::then([](std::string cfg) {
          std::printf("  Module B using: %s\n", cfg.c_str());
          return 2;
        });

    auto module_c = shared_config
      | ex::then([](std::string cfg) {
          std::printf("  Module C using: %s\n", cfg.c_str());
          return 3;
        });

    // All modules run with the same config (fetched once)
    // 모든 모듈이 동일한 설정으로 실행 (한 번만 가져옴)
    auto all_modules = ex::when_all(
      std::move(module_a),
      std::move(module_b),
      std::move(module_c)
    );

    auto result = ex::sync_wait(std::move(all_modules));
    if (result) {
      auto [a, b, c] = *result;
      std::printf("All modules completed: %d, %d, %d\n", a, b, c);
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  // Part 6: split() vs recreating senders
  //////////////////////////////////////////////////////////////////////////////
  std::printf("\n--- Part 6: split() vs factory functions ---\n");
  {
    // Alternative to split(): Create new sender instances
    // This runs the computation EACH time
    //
    // split()의 대안: 새 sender 인스턴스 생성
    // 이 경우 매번 계산이 실행됩니다

    auto make_computation = []() {
      return ex::just(42)
        | ex::then([](int x) {
            std::printf("  [Running computation]\n");
            return x * 2;
          });
    };

    std::printf("Without split (factory function - runs twice):\n");
    ex::sync_wait(make_computation());
    ex::sync_wait(make_computation());

    std::printf("\nWith split (runs once, result shared):\n");
    auto shared = ex::split(make_computation());
    ex::sync_wait(shared);
    ex::sync_wait(shared);
  }

  std::printf("\n=== End of Example 07 ===\n");
  return 0;
}
