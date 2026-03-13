/*
 * stdexec Playground - Asynchronous Handler Chaining (Advanced)
 *
 * 비동기 핸들러 체이닝은 HTTP 서버, RPC 프레임워크, 데이터 파이프라인 등에서
 * 흔히 쓰이는 패턴입니다. 요청이 여러 핸들러를 순서대로 통과하며 처리됩니다.
 *
 * 이 파일에서는 stdexec의 let_value / upon_error / let_stopped를 이용하여
 * 실용적인 핸들러 체인을 구성하는 방법을 설명합니다.
 *
 * 주요 설계 결정:
 *   - 에러 채널은 exception_ptr로 통일 (표준 C++ 예외 메커니즘 활용)
 *   - 각 핸들러는 then() 내부에서 throw로 에러 전달
 *   - upon_error(exception_ptr)로 에러를 응답으로 변환
 *   - middleware는 단일 반환 타입을 갖도록 then()만 사용
 *
 * 빌드:
 *   cmake --build build --target playground.handler_chaining
 *   ./build/playground/playground.handler_chaining
 */

#include <stdexec/execution.hpp>

#include <exec/async_scope.hpp>
#include <exec/single_thread_context.hpp>
#include <exec/static_thread_pool.hpp>

#include <atomic>
#include <cstdio>
#include <string>
#include <vector>

namespace ex   = stdexec;
namespace exec = experimental::execution;

static void section(char const * title)
{
  std::printf("\n=== %s ===\n", title);
}

///////////////////////////////////////////////////////////////////////////////
// 도메인 타입 정의
///////////////////////////////////////////////////////////////////////////////

struct http_request
{
  std::string method;
  std::string path;
  std::string body;
};

struct http_response
{
  int         status;
  std::string body;
};

struct parsed_request
{
  http_request original;
  std::string  resource_id;
};

struct process_result
{
  std::string data;
  int         version;
};

///////////////////////////////////////////////////////////////////////////////
// 1. 기본 핸들러 체인 (then + 예외 기반)
//
// 각 핸들러는 then() 내부에서 실행됩니다.
// 에러 발생 시 예외를 throw하면 stdexec가 자동으로 exception_ptr로 래핑하여
// set_error 채널로 전달합니다.
//
// 이렇게 하면 모든 핸들러의 에러 채널이 exception_ptr로 통일되어
// 하나의 upon_error로 처리할 수 있습니다.
///////////////////////////////////////////////////////////////////////////////

// 1단계: 유효성 검사 (예외로 에러 전달)
auto validate(http_request const & req) -> http_request
{
  std::printf("  [validate] %s %s\n", req.method.c_str(), req.path.c_str());
  if (req.method != "GET" && req.method != "POST")
    throw std::runtime_error{"method not allowed: " + req.method};
  if (req.path.empty() || req.path[0] != '/')
    throw std::runtime_error{"invalid path: " + req.path};
  return req;
}

// 2단계: 파싱 (리소스 ID 추출)
auto parse(http_request const & req) -> parsed_request
{
  auto        slash = req.path.rfind('/');
  std::string id    = (slash != std::string::npos && slash + 1 < req.path.size())
                      ? req.path.substr(slash + 1)
                      : "";
  std::printf("  [parse] resource_id='%s'\n", id.c_str());
  if (id.empty())
    throw std::runtime_error{"cannot parse resource id from: " + req.path};
  return {req, std::move(id)};
}

// 3단계: 비즈니스 로직 처리
auto process(parsed_request const & req) -> process_result
{
  std::printf("  [process] fetching resource='%s'\n", req.resource_id.c_str());
  return {"content of " + req.resource_id, 1};
}

// 4단계: 응답 직렬화
auto serialize(process_result const & r) -> http_response
{
  std::string body = "{\"data\":\"" + r.data + "\",\"v\":" + std::to_string(r.version) + "}";
  std::printf("  [serialize] body=%s\n", body.c_str());
  return {200, std::move(body)};
}

static void example_basic_chain()
{
  section("1. 기본 핸들러 체인: validate -> parse -> process -> serialize");

  auto handle = [](http_request req)
  {
    return ex::just(std::move(req))
         | ex::then(validate)  // 에러 시 예외 throw -> exception_ptr 에러 채널
         | ex::then(parse) | ex::then(process)
         | ex::then(serialize)
         // 모든 단계의 에러(exception_ptr)를 에러 응답으로 변환
         | ex::upon_error(
             [](std::exception_ptr ep) -> http_response
             {
               STDEXEC_TRY
               {
                 std::rethrow_exception(ep);
               }
               STDEXEC_CATCH(std::exception & e)
               {
                 std::printf("  [error] %s\n", e.what());
                 return {400, e.what()};
               }
               return {500, "unknown error"};
             });
  };

  std::printf("\n  -- 성공 요청 --\n");
  auto [r1] = ex::sync_wait(handle({"GET", "/resource/42", ""})).value();
  std::printf("  => %d %s\n\n", r1.status, r1.body.c_str());

  std::printf("  -- 잘못된 메서드 --\n");
  auto [r2] = ex::sync_wait(handle({"DELETE", "/resource/42", ""})).value();
  std::printf("  => %d %s\n\n", r2.status, r2.body.c_str());

  std::printf("  -- 리소스 ID 없음 --\n");
  auto [r3] = ex::sync_wait(handle({"GET", "/", ""})).value();
  std::printf("  => %d %s\n", r3.status, r3.body.c_str());
}

///////////////////////////////////////////////////////////////////////////////
// 2. 실행 컨텍스트 전환이 있는 핸들러 체인
//
//   I/O 스레드: 요청 수신 / 응답 전송
//   워커 스레드: CPU 집약적인 처리 로직
//
// continues_on(scheduler)으로 각 단계의 실행 컨텍스트를 전환합니다.
///////////////////////////////////////////////////////////////////////////////
static void example_context_switching_chain()
{
  section("2. 실행 컨텍스트 전환이 있는 핸들러 체인");

  exec::single_thread_context io_ctx;
  exec::static_thread_pool    workers{4};

  auto io_sched     = io_ctx.get_scheduler();
  auto worker_sched = workers.get_scheduler();

  auto handle = [&](http_request req)
  {
    return ex::starts_on(io_sched, ex::just(std::move(req)))
         | ex::then(
             [](http_request r)
             {
               std::printf("  [recv]    thread=%p (I/O)\n", (void*) pthread_self());
               return r;
             })
         // 워커 스레드로 전환하여 처리
         | ex::continues_on(worker_sched)
         | ex::then(
             [](http_request r)
             {
               std::printf("  [process] thread=%p (worker)\n", (void*) pthread_self());
               return http_response{200, "processed: " + r.path};
             })
         // I/O 스레드로 복귀
         | ex::continues_on(io_sched)
         | ex::then(
             [](http_response resp)
             {
               std::printf("  [send]    thread=%p (I/O)\n", (void*) pthread_self());
               return resp;
             });
  };

  auto [resp] = ex::sync_wait(handle({"GET", "/api/data", ""})).value();
  std::printf("  => %d %s\n", resp.status, resp.body.c_str());

  workers.request_stop();
}

///////////////////////////////////////////////////////////////////////////////
// 3. Middleware 패턴
//
// Middleware는 핸들러를 감싸는 래퍼입니다.
// 각 middleware는 핸들러 함수를 받아 확장된 핸들러를 반환합니다.
//
//   with_logging(with_auth(core_handler))
//
// 주의: middleware 내부에서는 단일 타입의 sender만 반환해야 합니다.
//       조건 분기로 다른 타입의 sender를 반환할 경우 컴파일 에러가 발생합니다.
//       이 예제에서는 upon_error와 예외를 활용하여 단일 타입을 유지합니다.
///////////////////////////////////////////////////////////////////////////////

// 핵심 핸들러
auto core_handler(http_request req) -> http_response
{
  std::printf("  [core] handling %s\n", req.path.c_str());
  return {200, "OK: " + req.path};
}

// Middleware: 로깅 (before/after 시간 기록)
// handler가 값(T)을 반환하는 함수일 때 사용
template <class Handler>
auto with_logging(Handler handler)
{
  return [handler = std::move(handler)](http_request req) mutable
  {
    std::printf("  [log] --> %s %s\n", req.method.c_str(), req.path.c_str());
    return ex::just(std::move(req))
         | ex::then(handler)  // handler는 http_response 값을 반환하는 함수
         | ex::then(
             [](http_response resp)
             {
               std::printf("  [log] <-- %d\n", resp.status);
               return resp;
             });
  };
}

// Middleware: 인증 확인
// 인증 실패 시 예외를 throw, upon_error로 401 응답으로 변환
template <class Handler>
auto with_auth(Handler handler)
{
  return [handler = std::move(handler)](http_request req) mutable -> http_response
  {
    bool needs_auth = req.path.find("/admin") != std::string::npos;
    bool has_token  = req.body.find("token=valid") != std::string::npos;
    if (needs_auth && !has_token)
    {
      std::printf("  [auth] DENIED\n");
      return {401, "Unauthorized"};
    }
    std::printf("  [auth] OK\n");
    return handler(std::move(req));
  };
}

static void example_middleware_pattern()
{
  section("3. Middleware 패턴 (logging -> auth -> core)");

  auto pipeline = with_logging(with_auth(core_handler));

  auto send = [&](char const * label, http_request req)
  {
    std::printf("\n  -- %s --\n", label);
    auto [resp] = ex::sync_wait(pipeline(std::move(req))).value();
    std::printf("  result: %d %s\n", resp.status, resp.body.c_str());
  };

  send("일반 요청", {"GET", "/api/data", ""});
  send("관리자 경로 (인증 없음)", {"GET", "/admin/users", ""});
  send("관리자 경로 (인증 있음)", {"GET", "/admin/users", "token=valid"});
}

///////////////////////////////////////////////////////////////////////////////
// 4. 다중 요청 병렬 처리 + async_scope
//
// 실제 서버에서는 요청을 병렬로 처리합니다.
// async_scope를 사용하면 각 요청을 독립적인 비동기 작업으로 관리할 수 있습니다.
///////////////////////////////////////////////////////////////////////////////
static void example_parallel_request_handling()
{
  section("4. 다중 요청 병렬 처리 (async_scope)");

  exec::static_thread_pool pool{4};
  auto                     sched = pool.get_scheduler();
  exec::async_scope        scope;

  std::atomic<int> success_count{0};
  std::atomic<int> error_count{0};

  auto handle_one = [&](int id, http_request req)
  {
    return ex::starts_on(sched,
                         ex::just(std::move(req)) | ex::then(validate) | ex::then(parse)
                           | ex::then(process) | ex::then(serialize)
                           | ex::then(
                             [&, id](http_response resp)
                             {
                               success_count.fetch_add(1, std::memory_order_relaxed);
                               std::printf("  [%d] => %d\n", id, resp.status);
                             })
                           | ex::upon_error(
                             [&, id](std::exception_ptr ep)
                             {
                               error_count.fetch_add(1, std::memory_order_relaxed);
                               STDEXEC_TRY
                               {
                                 std::rethrow_exception(ep);
                               }
                               STDEXEC_CATCH(std::exception & e)
                               {
                                 std::printf("  [%d] => ERROR: %s\n", id, e.what());
                               }
                             }));
  };

  std::vector<http_request> requests = {
    {   "GET", "/resource/1", ""}, // 성공
    {   "GET", "/resource/2", ""}, // 성공
    {"DELETE", "/resource/3", ""}, // 실패: 잘못된 메서드
    {   "GET", "/resource/4", ""}, // 성공
    {   "GET",           "/", ""}, // 실패: ID 없음
    {   "GET", "/resource/6", ""}, // 성공
  };

  for (int i = 0; i < static_cast<int>(requests.size()); ++i)
    scope.spawn(handle_one(i + 1, requests[i]));

  ex::sync_wait(scope.on_empty());
  std::printf("  total: success=%d, error=%d\n", success_count.load(), error_count.load());

  pool.request_stop();
}

///////////////////////////////////////////////////////////////////////////////
// 5. 취소 전파가 있는 핸들러 체인
//
// let_stopped 핸들러를 체인 끝에 추가하면
// 어느 단계에서든 취소 신호가 발생했을 때 503 응답을 생성할 수 있습니다.
///////////////////////////////////////////////////////////////////////////////
static void example_cancellation_in_chain()
{
  section("5. 취소 전파가 있는 핸들러 체인");

  auto error_handler = ex::upon_error(
    [](std::exception_ptr ep) -> http_response
    {
      STDEXEC_TRY
      {
        std::rethrow_exception(ep);
      }
      STDEXEC_CATCH(std::exception & e)
      {
        return {400, e.what()};
      }
      return {500, "error"};
    });

  auto stopped_handler = ex::let_stopped(
    []
    {
      std::printf("  [stopped] 요청 취소됨\n");
      return ex::just(http_response{503, "Service Unavailable"});
    });

  auto full_chain = [&](http_request req)
  {
    return ex::just(std::move(req)) | ex::then(validate) | ex::then(parse) | ex::then(process)
         | ex::then(serialize) | error_handler | stopped_handler;
  };

  // 정상 처리
  std::printf("\n  -- 정상 처리 --\n");
  auto [r1] = ex::sync_wait(full_chain({"GET", "/resource/7", ""})).value();
  std::printf("  => %d %s\n", r1.status, r1.body.c_str());

  // 에러 처리
  std::printf("\n  -- 유효성 검사 실패 --\n");
  auto [r2] = ex::sync_wait(full_chain({"PATCH", "/resource/7", ""})).value();
  std::printf("  => %d %s\n", r2.status, r2.body.c_str());

  // 취소 처리
  std::printf("\n  -- 취소된 요청 --\n");
  auto [r3] = ex::sync_wait(ex::just_stopped() | error_handler | stopped_handler).value();
  std::printf("  => %d %s\n", r3.status, r3.body.c_str());
}

///////////////////////////////////////////////////////////////////////////////
// main
///////////////////////////////////////////////////////////////////////////////
auto main() -> int
{
  std::printf("stdexec Playground - Asynchronous Handler Chaining\n");
  std::printf("====================================================\n");

  example_basic_chain();
  example_context_switching_chain();
  example_middleware_pattern();
  example_parallel_request_handling();
  example_cancellation_in_chain();

  std::printf("\n====================================================\n");
  std::printf("Done!\n");
  return 0;
}
