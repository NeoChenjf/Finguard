#pragma once

#include <drogon/drogon.h>
#include <functional>

namespace finguard::valuation {

// POST /api/v1/valuecell 路由处理器
// 请求体：{"symbol": "AAPL"}
// 返回：JSON 含 financial_metrics + safety_margin + qualitative_analysis
void handle_valuecell(const drogon::HttpRequestPtr &req,
                      std::function<void(const drogon::HttpResponsePtr &)> &&cb);

} // namespace finguard::valuation
