#include <gtest/gtest.h>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

#include "server/routes_internal.h"

TEST(RouteSupport, TraceIdUsesHeaderWhenPresent) {
    auto req = drogon::HttpRequest::newHttpRequest();
    req->addHeader("X-Trace-Id", "trace-123");

    EXPECT_EQ(finguard::server::internal::get_or_create_trace_id(req), "trace-123");
}

TEST(RouteSupport, TraceIdFallsBackToGeneratedUuid) {
    auto req = drogon::HttpRequest::newHttpRequest();
    const auto trace_id = finguard::server::internal::get_or_create_trace_id(req);

    EXPECT_FALSE(trace_id.empty());
}

TEST(RouteSupport, EntryKeyUsesUserAndRoute) {
    auto req = drogon::HttpRequest::newHttpRequest();
    req->addHeader("X-User-Id", "alice");
    req->setPath("/api/v1/plan");

    EXPECT_EQ(finguard::server::internal::make_entry_key(req),
              "entry:user:alice:route:/api/v1/plan");
}

TEST(RouteSupport, RateLimitErrorBodyKeepsWireShape) {
    const auto body = finguard::server::internal::rate_limit_error_body();

    EXPECT_TRUE(body.isMember("error"));
    EXPECT_EQ(body["error"]["code"].asString(), "RATE_LIMITED");
    EXPECT_EQ(body["error"]["message"].asString(), "rate limited");
    EXPECT_EQ(body["error"]["retry_after_ms"].asInt(), 1000);
}

TEST(RouteSupport, CorsHeadersKeepExpectedValues) {
    auto resp = drogon::HttpResponse::newHttpResponse();
    finguard::server::internal::add_cors_headers(resp);

    EXPECT_EQ(resp->getHeader("Access-Control-Allow-Origin"), "*");
    EXPECT_EQ(resp->getHeader("Access-Control-Allow-Methods"), "GET, POST, OPTIONS");
    EXPECT_EQ(resp->getHeader("Access-Control-Allow-Headers"),
              "Content-Type, X-User-Id, X-Trace-Id, X-Api-Key");
}
