# Basic Examples - Sender 기반 비동기 실행 모델

이 폴더에는 C++ P2300 **sender 기반 비동기 실행 모델**의 핵심 개념을 학습할 수 있는 기본 예제들이 포함되어 있습니다.

## 예제 목록

| 파일 | 학습 목표 | 주요 API |
|------|----------|----------|
| **01_just_and_sync_wait.cpp** | Sender 기본 개념, 값 생성과 대기 | `just()`, `sync_wait()` |
| **02_then.cpp** | 연속 작업 체이닝, pipe 연산자 | `then()`, `\|` |
| **03_let_value.cpp** | 동적으로 sender 생성 | `let_value()` |
| **04_schedule.cpp** | 스케줄러와 실행 컨텍스트 | `schedule()`, `static_thread_pool` |
| **05_when_all.cpp** | 병렬 실행, 결과 조합 | `when_all()` |
| **06_starts_on_continues_on.cpp** | 실행 컨텍스트 전환 | `starts_on()`, `continues_on()` |
| **07_split.cpp** | Sender 결과 공유 | `split()` |

## 학습 순서

예제는 번호 순서대로 학습하는 것을 권장합니다:

1. **01 - just & sync_wait**: Sender가 무엇인지, 어떻게 값을 생성하고 결과를 얻는지 이해
2. **02 - then**: Sender를 체이닝하여 연속 작업 수행, pipe operator 사용법
3. **03 - let_value**: `then()`과의 차이점, 동적으로 sender를 생성하는 방법
4. **04 - schedule**: 스케줄러 개념, thread pool에서 작업 실행
5. **05 - when_all**: 여러 작업을 병렬로 실행하고 결과 조합
6. **06 - starts_on & continues_on**: 실행 컨텍스트 간 작업 전환
7. **07 - split**: 단일 사용 sender를 다중 사용 가능하게 변환

## 빌드 방법

### 컴파일러 요구사항

stdexec는 C++20 기능을 완전히 지원하는 컴파일러가 필요합니다:

| 컴파일러 | 최소 버전 |
|---------|----------|
| GCC | 11+ |
| Clang | 16+ |
| nvc++ | 22.11+ |

> **Note**: Apple Clang (Xcode 기본)은 일부 C++20 기능(`<source_location>` 등)을 지원하지 않아 빌드가 실패합니다.

### macOS에서 빌드 (Homebrew LLVM 사용)

```bash
# 1. LLVM 설치 (아직 설치하지 않은 경우)
brew install llvm

# 2. 프로젝트 루트에서 CMake 설정 (LLVM Clang 지정)
cmake -S . -B build -G Ninja \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++

# 3. Basic 예제 전체 빌드
cmake --build build --target basic_examples

# 또는 개별 예제 빌드
cmake --build build --target example.basic.01_just_and_sync_wait
cmake --build build --target example.basic.02_then
# ... 등등
```

### Linux에서 빌드 (GCC 또는 Clang)

```bash
# GCC 사용
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=g++

# 또는 Clang 사용
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++

# Basic 예제 전체 빌드
cmake --build build --target basic_examples
```

## 실행 방법

### CTest로 실행 (권장)

```bash
# 프로젝트 루트에서 basic 예제 테스트 실행
ctest --test-dir build -R "example.basic" --output-on-failure

# 또는 build 디렉토리에서
cd build && ctest -R "example.basic" --output-on-failure
```

### 개별 실행

```bash
./build/examples/basic/example.basic.01_just_and_sync_wait
./build/examples/basic/example.basic.02_then
./build/examples/basic/example.basic.03_let_value
./build/examples/basic/example.basic.04_schedule
./build/examples/basic/example.basic.05_when_all
./build/examples/basic/example.basic.06_starts_on_continues_on
./build/examples/basic/example.basic.07_split
```

## 핵심 개념 요약

### Sender란?
- **비동기 작업의 설명(description)** 을 나타내는 객체
- Lazy evaluation: 생성 시점이 아닌 실행 시점에 작업이 수행됨
- 값, 에러, 또는 취소 신호로 완료될 수 있음

### 주요 알고리즘

| 알고리즘 | 설명 |
|---------|------|
| `just(values...)` | 주어진 값을 즉시 생성하는 sender |
| `then(fn)` | 이전 결과에 함수를 적용하여 변환 |
| `let_value(fn)` | 이전 결과로 새 sender를 동적 생성 |
| `schedule(sched)` | 스케줄러에서 void sender 시작 |
| `when_all(senders...)` | 여러 sender를 병렬 실행, 모든 결과 조합 |
| `starts_on(sched, snd)` | sender를 특정 스케줄러에서 시작 |
| `continues_on(sched)` | 이후 작업을 다른 스케줄러로 전환 |
| `split(snd)` | 단일 사용 sender를 다중 사용 가능하게 변환 |
| `sync_wait(snd)` | sender를 실행하고 결과를 동기적으로 대기 |

### Pipe Operator

```cpp
// 함수 호출 스타일
auto snd = then(then(just(42), f1), f2);

// Pipe operator 스타일 (권장)
auto snd = just(42) | then(f1) | then(f2);
```

## 참고 자료

- [P2300 - std::execution](https://wg21.link/p2300): C++ 표준 제안서
- [NVIDIA/stdexec README](../../../README.md): 프로젝트 메인 문서
- [Working with Asynchrony Generically](https://www.youtube.com/watch?v=xLboNIf7BTg): 비디오 튜토리얼
