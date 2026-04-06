#include <gtest/gtest.h>

#include "llm/llm_client_internal.h"

TEST(LlmHelpers, ParseBaseKeepsSchemePortAndPrefix) {
    const auto info = finguard::llm::internal::parse_base("https://example.com:8443/v1/test/");

    EXPECT_EQ(info.host, "example.com");
    EXPECT_EQ(info.prefix, "/v1/test");
    EXPECT_EQ(info.port, 8443);
    EXPECT_TRUE(info.use_ssl);
}

TEST(LlmHelpers, ParseProxySupportsHttpProxy) {
    const auto proxy = finguard::llm::internal::parse_proxy("http://127.0.0.1:7890");

    EXPECT_TRUE(proxy.enabled);
    EXPECT_EQ(proxy.host, "127.0.0.1");
    EXPECT_EQ(proxy.port, 7890);
    EXPECT_FALSE(proxy.use_ssl);
}

TEST(LlmHelpers, JoinUrlAvoidsDoubleSlash) {
    EXPECT_EQ(finguard::llm::internal::join_url("https://example.com/v1/", "/chat/completions"),
              "https://example.com/v1/chat/completions");
}
