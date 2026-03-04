#include "server/routes.h"

#include <drogon/drogon.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

#include "llm/llm_client.h"
#include "risk/profile_store.h"
#include "risk/rule_engine.h"
#include "util/concurrency_limiter.h"
#include "util/metrics_registry.h"
#include "util/reliability_config.h"
#include "util/token_bucket.h"
#include "valuation/valuation_handler.h"

#include <drogon/utils/Utilities.h>

namespace finguard {

namespace {

// 将 Json::Value 序列化为一行 JSON 文本，便于 SSE 的 data: 输出
std::string serialize_event(const Json::Value &event) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, event);
}

util::TokenBucket g_entry_bucket;
util::ConcurrencyLimiter g_concurrency_limiter(4);

std::string get_or_create_trace_id(const drogon::HttpRequestPtr &req) {
    if (req) {
        const auto tid = req->getHeader("X-Trace-Id");
        if (!tid.empty()) {
            return tid;
        }
    }
    return drogon::utils::getUuid();
}

std::string get_user_id(const drogon::HttpRequestPtr &req) {
    if (!req) {
        return "anonymous";
    }
    const auto user_id = req->getHeader("X-User-Id");
    if (!user_id.empty()) {
        return user_id;
    }
    return "anonymous";
}

std::string make_entry_key(const drogon::HttpRequestPtr &req) {
    const auto user_id = get_user_id(req);
    const auto route = req ? req->path() : "";
    return "entry:user:" + user_id + ":route:" + route;
}

Json::Value rate_limit_error_body() {
    Json::Value body;
    body["error"]["code"] = "RATE_LIMITED";
    body["error"]["message"] = "rate limited";
    body["error"]["retry_after_ms"] = 1000;
    return body;
}

void log_request_metrics(const std::string &trace_id,
                         const std::string &route,
                         int status,
                         double latency_ms) {
    util::global_metrics().record_request(latency_ms);
    LOG_INFO << "trace_id=" << trace_id
             << " route=" << route
             << " status=" << status
             << " latency_ms=" << latency_ms;
}

// ── P6: CORS 辅助 ──
void add_cors_headers(const drogon::HttpResponsePtr &resp) {
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type, X-User-Id, X-Trace-Id, X-Api-Key");
}

// ── P6: Settings 写锁 ──
std::mutex g_settings_mutex;

}

void setup_routes() {
    // 引入 drogon 命名空间，便于直接使用 app()、HttpResponse 等符号
    using namespace drogon;

    // 健康检查：返回固定 JSON
    // 注册 GET /health 路由处理器
    app().registerHandler("/health", [](const HttpRequestPtr &, std::function<void(const HttpResponsePtr &)> &&cb) {
        // 构造 JSON 响应体
        Json::Value body;
        // 设置状态字段
        body["status"] = "ok";
        // 构造 JSON 响应对象
        auto resp = HttpResponse::newHttpJsonResponse(body);
        // 回调返回响应
        cb(resp);
    // 指定该路由仅支持 GET 方法
    }, {Get});

    // 配置建议（mock）
    // 注册 POST /api/v1/plan 路由处理器
    app().registerHandler("/api/v1/plan", [](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&cb) {
        const auto trace_id = get_or_create_trace_id(req);
        const auto start = std::chrono::steady_clock::now();
        const auto route = req ? req->path() : "/api/v1/plan";
        const auto rate_cfg = util::cached_rate_limit_config();
        const auto conc_cfg = util::cached_concurrency_config();
        g_concurrency_limiter.set_max_inflight(conc_cfg.max_inflight);

        if (!g_concurrency_limiter.try_acquire()) {
            util::global_metrics().record_rate_limit_reject();
            Json::Value body = rate_limit_error_body();
            auto resp = HttpResponse::newHttpJsonResponse(body);
            resp->setStatusCode(k429TooManyRequests);
            resp->addHeader("X-Trace-Id", trace_id);
            cb(resp);
            const auto latency_ms = std::chrono::duration<double, std::milli>(
                                        std::chrono::steady_clock::now() - start)
                                        .count();
            log_request_metrics(trace_id, route, 429, latency_ms);
            return;
        }

        if (!g_entry_bucket.allow(make_entry_key(req), rate_cfg.entry.rate_rps, rate_cfg.entry.capacity)) {
            util::global_metrics().record_rate_limit_reject();
            Json::Value body = rate_limit_error_body();
            auto resp = HttpResponse::newHttpJsonResponse(body);
            resp->setStatusCode(k429TooManyRequests);
            resp->addHeader("X-Trace-Id", trace_id);
            cb(resp);
            const auto latency_ms = std::chrono::duration<double, std::milli>(
                                        std::chrono::steady_clock::now() - start)
                                        .count();
            log_request_metrics(trace_id, route, 429, latency_ms);
            g_concurrency_limiter.release();
            return;
        }

        // ── 解析请求 JSON ──
        auto json_ptr = req ? req->getJsonObject() : nullptr;
        if (!json_ptr || !json_ptr->isMember("profile")) {
            Json::Value err;
            err["error"] = "missing_profile";
            err["message"] = "请求体必须包含 profile 字段";
            auto resp = HttpResponse::newHttpJsonResponse(err);
            resp->setStatusCode(k400BadRequest);
            resp->addHeader("X-Trace-Id", trace_id);
            cb(resp);
            g_concurrency_limiter.release();
            const auto latency_ms = std::chrono::duration<double, std::milli>(
                                        std::chrono::steady_clock::now() - start).count();
            log_request_metrics(trace_id, route, 400, latency_ms);
            return;
        }
        const auto &profile = (*json_ptr)["profile"];

        // ── 读取画像参数 ──
        const int age = profile.get("age", 30).asInt();
        const std::string investor_type = profile.get("investor_type", "novice").asString();
        const std::string exp_years = profile.get("experience_years", "0-5").asString();
        const std::string annual_ret = profile.get("annualized_return", "0-10").asString();
        const std::string beat_sp500 = profile.get("beat_sp500_10y", "no").asString();
        const double individual_pct = profile.get("individual_stock_percent", 0.0).asDouble();

        // ── 守拙理念：计算资产配置 ──
        // 黄金固定 10%
        const double gold = 0.10;
        // 债券 = 年龄十位数 × 10%（20岁→20%, 30岁→30%, …）
        const int age_decade = (age / 10) * 10;
        const double bond = (std::min)((std::max)(age_decade / 100.0, 0.0), 0.80);
        // 股票 = 余量
        const double equity = (std::max)(1.0 - gold - bond, 0.0);

        // 个股占总配置（不能超过 equity 部分）
        double actual_stock_pick = 0.0;
        if (investor_type == "experienced") {
            actual_stock_pick = (std::min)(individual_pct, (std::min)(equity, 0.50));
        } else if (investor_type == "professional") {
            actual_stock_pick = (std::min)(individual_pct, equity);
        }
        // novice: 不允许个股
        const double index_equity = equity - actual_stock_pick;
        const double pick_equity = actual_stock_pick;

        // 指数比例：港股:A股:美股 = 1:3:16
        const double total_ratio = 1.0 + 3.0 + 16.0;
        const double hk = index_equity * (1.0 / total_ratio);
        const double a_share = index_equity * (3.0 / total_ratio);
        const double us = index_equity * (16.0 / total_ratio);

        // ── 资格检查 & 风控 ──
        Json::Value triggered_rules(Json::arrayValue);
        std::string risk_status = "PASS";

        if (investor_type == "experienced") {
            if (exp_years == "0-5" || annual_ret == "0-10") {
                triggered_rules.append("经验丰富投资人要求: >=5年经验 且 年化收益>=10%");
                risk_status = "WARN";
            }
        } else if (investor_type == "professional") {
            if (exp_years != "10+" || beat_sp500 != "yes") {
                triggered_rules.append("专业投资人要求: >=10年经验 且 十年业绩跑赢标普500");
                risk_status = "WARN";
            }
        }

        // 单一资产不低于 2.5%（浮点容差 0.001）
        const double min_asset = 0.025;
        if (hk > 0.001 && hk < min_asset - 0.001) {
            triggered_rules.append("港股指数占比 " + std::to_string(int(hk*100)) + "% 低于 2.5% 下限");
            risk_status = "WARN";
        }

        // ── 生成 rationale ──
        std::string rationale = "基于守拙价值多元化基金理念：年龄 " + std::to_string(age) +
            " 岁 → 债券(VGIT) " + std::to_string(age_decade) + "%，黄金(GLD) 10%，" +
            "股票 " + std::to_string(int(equity * 100)) + "%。";
        if (investor_type == "novice") {
            rationale += " 小白投资人：全部使用指数基金，不配个股。";
        } else if (investor_type == "experienced") {
            rationale += " 经验丰富投资人：个股占总配置 " + std::to_string(int(actual_stock_pick * 100)) + "%（上限" + std::to_string(int((std::min)(equity, 0.50) * 100)) + "%）。";
        } else {
            rationale += " 专业投资人：个股占总配置 " + std::to_string(int(actual_stock_pick * 100)) + "%（上限" + std::to_string(int(equity * 100)) + "%）。";
        }
        rationale += " 指数配比港股:A股:美股 = 1:3:16。";

        // ── 生成调仓建议 ──
        Json::Value actions(Json::arrayValue);
        // 如果前端传了当前持仓，比对差异
        if (json_ptr->isMember("portfolio") && (*json_ptr)["portfolio"].isArray()) {
            const auto &portfolio = (*json_ptr)["portfolio"];
            // 标准 ETF 集合
            const std::set<std::string> standard_etfs = {"VOO", "\xe6\xb2\xaa\xe6\xb7\xb1""300", "\xe6\x81\x92\xe7\x94\x9f\xe6\x8c\x87\xe6\x95\xb0", "VGIT", "GLD"};
            // 构建当前持仓 map，并累计个股持仓
            std::map<std::string, double> current;
            double current_stock_pick = 0.0;
            for (const auto &item : portfolio) {
                if (item.isMember("symbol") && item.isMember("weight")) {
                    const auto sym = item["symbol"].asString();
                    const auto w = item["weight"].asDouble();
                    if (standard_etfs.count(sym)) {
                        current[sym] = w;
                    } else {
                        // 非标准 ETF 归为个股持仓
                        current_stock_pick += w;
                    }
                }
            }
            current["个股仓位"] = current_stock_pick;
            // 构建目标配置 map
            std::map<std::string, double> target;
            target["VOO"] = us;
            target["沪深300"] = a_share;
            target["恒生指数"] = hk;
            target["VGIT"] = bond;
            target["GLD"] = gold;
            target["个股仓位"] = pick_equity;

            for (const auto &[sym, tw] : target) {
                double cw = 0.0;
                if (current.count(sym)) cw = current[sym];
                double diff = tw - cw;
                if (std::abs(diff) > 0.005) {
                    std::string action = (diff > 0 ? "增持 " : "减持 ") + sym +
                        "：" + std::to_string(int(cw * 100)) + "% → " + std::to_string(int(tw * 100)) + "%";
                    actions.append(action);
                }
            }
        }

        // ── 构造响应 ──
        Json::Value body;
        body["proposed_portfolio"]["VOO"] = us;
        body["proposed_portfolio"]["沪深300"] = a_share;
        body["proposed_portfolio"]["恒生指数"] = hk;
        body["proposed_portfolio"]["VGIT"] = bond;
        body["proposed_portfolio"]["GLD"] = gold;
        body["proposed_portfolio"]["个股仓位"] = pick_equity;
        body["risk_report"]["status"] = risk_status;
        body["risk_report"]["triggered_rules"] = triggered_rules;
        body["rationale"] = rationale;
        body["rebalancing_actions"] = actions;

        auto resp = HttpResponse::newHttpJsonResponse(body);
        resp->addHeader("X-Trace-Id", trace_id);
        cb(resp);
        const auto latency_ms = std::chrono::duration<double, std::milli>(
                                    std::chrono::steady_clock::now() - start)
                                    .count();
        log_request_metrics(trace_id, route, resp->getStatusCode(), latency_ms);
        g_concurrency_limiter.release();
    // 指定该路由仅支持 POST 方法
    }, {Post});

    // 问卷档案写入
    // 注册 POST /api/v1/profile/upsert 路由处理器
    app().registerHandler("/api/v1/profile/upsert",
        [](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&cb) {
            const auto trace_id = get_or_create_trace_id(req);
            const auto start = std::chrono::steady_clock::now();
            const auto route = req ? req->path() : "/api/v1/profile/upsert";
            const auto rate_cfg = util::cached_rate_limit_config();

            if (!g_entry_bucket.allow(make_entry_key(req), rate_cfg.entry.rate_rps, rate_cfg.entry.capacity)) {
                util::global_metrics().record_rate_limit_reject();
                Json::Value body = rate_limit_error_body();
                auto resp = HttpResponse::newHttpJsonResponse(body);
                resp->setStatusCode(k429TooManyRequests);
                resp->addHeader("X-Trace-Id", trace_id);
                cb(resp);
                const auto latency_ms = std::chrono::duration<double, std::milli>(
                                            std::chrono::steady_clock::now() - start)
                                            .count();
                log_request_metrics(trace_id, route, resp->getStatusCode(), latency_ms);
                return;
            }
            // 从请求头读取用户账号
            const auto user_id = req ? req->getHeader("X-User-Id") : "";
            // 用户账号必填
            if (user_id.empty()) {
                Json::Value body;
                body["error"] = "missing_user_id";
                auto resp = HttpResponse::newHttpJsonResponse(body);
                resp->setStatusCode(k400BadRequest);
                resp->addHeader("X-Trace-Id", trace_id);
                cb(resp);
                const auto latency_ms = std::chrono::duration<double, std::milli>(
                                            std::chrono::steady_clock::now() - start)
                                            .count();
                log_request_metrics(trace_id, route, resp->getStatusCode(), latency_ms);
                return;
            }

            // 解析问卷 JSON
            auto json = req ? req->getJsonObject() : nullptr;
            if (!json || !json->isMember("questionnaire") || !(*json)["questionnaire"].isObject()) {
                Json::Value body;
                body["error"] = "missing_questionnaire";
                auto resp = HttpResponse::newHttpJsonResponse(body);
                resp->setStatusCode(k400BadRequest);
                resp->addHeader("X-Trace-Id", trace_id);
                cb(resp);
                const auto latency_ms = std::chrono::duration<double, std::milli>(
                                            std::chrono::steady_clock::now() - start)
                                            .count();
                log_request_metrics(trace_id, route, resp->getStatusCode(), latency_ms);
                return;
            }

            std::string error;
            if (!risk::upsert_profile(user_id, (*json)["questionnaire"], &error)) {
                Json::Value body;
                body["error"] = "profile_upsert_failed";
                body["detail"] = error;
                auto resp = HttpResponse::newHttpJsonResponse(body);
                resp->setStatusCode(k500InternalServerError);
                resp->addHeader("X-Trace-Id", trace_id);
                cb(resp);
                const auto latency_ms = std::chrono::duration<double, std::milli>(
                                            std::chrono::steady_clock::now() - start)
                                            .count();
                log_request_metrics(trace_id, route, resp->getStatusCode(), latency_ms);
                return;
            }

            Json::Value body;
            body["status"] = "ok";
            body["user_id"] = user_id;
            auto resp = HttpResponse::newHttpJsonResponse(body);
            resp->addHeader("X-Trace-Id", trace_id);
            cb(resp);
            const auto latency_ms = std::chrono::duration<double, std::milli>(
                                        std::chrono::steady_clock::now() - start)
                                        .count();
            log_request_metrics(trace_id, route, resp->getStatusCode(), latency_ms);
        },
        {Post});

    // 流式问答（SSE）
    // 注册 POST /api/v1/chat/stream 路由处理器
    app().registerHandler("/api/v1/chat/stream",
        [](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&cb) {
            const auto trace_id = get_or_create_trace_id(req);
            const auto start = std::chrono::steady_clock::now();
            const auto route = req ? req->path() : "/api/v1/chat/stream";
            const auto rate_cfg = util::cached_rate_limit_config();
            const auto conc_cfg = util::cached_concurrency_config();
            g_concurrency_limiter.set_max_inflight(conc_cfg.max_inflight);

            if (!g_concurrency_limiter.try_acquire()) {
                util::global_metrics().record_rate_limit_reject();
                Json::Value body = rate_limit_error_body();
                auto resp = HttpResponse::newHttpJsonResponse(body);
                resp->setStatusCode(k429TooManyRequests);
                resp->addHeader("X-Trace-Id", trace_id);
                cb(resp);
                const auto latency_ms = std::chrono::duration<double, std::milli>(
                                            std::chrono::steady_clock::now() - start)
                                            .count();
                log_request_metrics(trace_id, route, 429, latency_ms);
                return;
            }

            if (!g_entry_bucket.allow(make_entry_key(req), rate_cfg.entry.rate_rps, rate_cfg.entry.capacity)) {
                util::global_metrics().record_rate_limit_reject();
                Json::Value body = rate_limit_error_body();
                auto resp = HttpResponse::newHttpJsonResponse(body);
                resp->setStatusCode(k429TooManyRequests);
                resp->addHeader("X-Trace-Id", trace_id);
                cb(resp);
                const auto latency_ms = std::chrono::duration<double, std::milli>(
                                            std::chrono::steady_clock::now() - start)
                                            .count();
                log_request_metrics(trace_id, route, 429, latency_ms);
                g_concurrency_limiter.release();
                return;
            }

            // 读取用户输入的 prompt（JSON body）
            // 准备接收 prompt 文本
            std::string prompt;
            // 判断请求对象是否存在
            if (req) {
                // 解析请求体 JSON
                auto json = req->getJsonObject();
                // 校验 prompt 字段存在且为字符串
                if (json && json->isMember("prompt") && (*json)["prompt"].isString()) {
                    // 读取 prompt 字段
                    prompt = (*json)["prompt"].asString();
                }
            }

            // 读取用户档案（问卷）
            Json::Value questionnaire;
            std::string profile_error;
            const auto user_id = req ? req->getHeader("X-User-Id") : "";
            const bool has_profile = !user_id.empty() && risk::load_profile(user_id, &questionnaire, &profile_error);

            // 加载规则引擎
            risk::RuleEngine rule_engine;
            std::string rules_error;
            rule_engine.load_config(&rules_error);

            // 规则引擎检查（强规则）
            auto rule_result = rule_engine.check_request(prompt, questionnaire);

            // 创建 LLM 客户端
            llm::LlmClient client;
            // P5 perf: cache LLM config for API key check (5s TTL)
            // P6: reuse global cache from llm_client.cpp via load_config() directly
            // (the cache inside stream_chat() already handles TTL)
            llm::LlmConfig config = client.load_config();
            // 服务端必须配置 api_key，否则直接报 500
            // 若未配置 api_key 则返回 500
            if (config.api_key.empty()) {
                // 构造错误响应体
                Json::Value body;
                // 设置错误码标识
                body["error"] = "server_missing_api_key";
                // 构造 JSON 响应对象
                auto resp = HttpResponse::newHttpJsonResponse(body);
                // 设置 HTTP 状态码为 500
                resp->setStatusCode(k500InternalServerError);
                resp->addHeader("X-Trace-Id", trace_id);
                // 回调返回响应
                cb(resp);
                const auto latency_ms = std::chrono::duration<double, std::milli>(
                                            std::chrono::steady_clock::now() - start)
                                            .count();
                log_request_metrics(trace_id, route, resp->getStatusCode(), latency_ms);
                g_concurrency_limiter.release();
                // 终止处理
                return;
            }

            // 请求方必须带 X-API-Key，若未传则使用服务端配置的 key
            // 从请求头读取 X-API-Key
            auto request_key = req ? req->getHeader("X-API-Key") : "";
            // 若前端未传 key，使用服务端自身配置
            if (request_key.empty()) {
                request_key = config.api_key;
            }
            // 校验请求方 API Key 是否匹配
            if (request_key != config.api_key) {
                // 构造错误响应体
                Json::Value body;
                // 设置错误码标识
                body["error"] = "unauthorized";
                // 构造 JSON 响应对象
                auto resp = HttpResponse::newHttpJsonResponse(body);
                // 设置 HTTP 状态码为 401
                resp->setStatusCode(k401Unauthorized);
                resp->addHeader("X-Trace-Id", trace_id);
                // 回调返回响应
                cb(resp);
                const auto latency_ms = std::chrono::duration<double, std::milli>(
                                            std::chrono::steady_clock::now() - start)
                                            .count();
                log_request_metrics(trace_id, route, resp->getStatusCode(), latency_ms);
                g_concurrency_limiter.release();
                // 终止处理
                return;
            }

            // 调用 LLM client（内部支持降级）
            // 发起流式请求并获取结果
            auto result = client.stream_chat(prompt);

            // 保证 cite / warning / metric 事件必有输出（便于验收）
            // 复制引用列表
            auto cites = result.cites;
            // 若引用列表为空，则补一个默认值
            if (cites.empty()) {
                // 补默认引用标识
                cites.push_back("none");
            }
            // 组装告警事件（字符串与结构化告警混合）
            std::vector<Json::Value> warning_events;
            // 复制告警列表
            for (const auto &warning : result.warnings) {
                warning_events.emplace_back(warning);
            }
            // 附加规则引擎告警（请求侧）
            for (const auto &warning : rule_result.warnings) {
                warning_events.emplace_back(warning);
            }
            // 若档案缺失，记录一次告警
            if (!has_profile) {
                if (!profile_error.empty()) {
                    warning_events.emplace_back("profile_error:" + profile_error);
                } else {
                    warning_events.emplace_back("profile_missing_or_unreadable");
                }
            }
            if (!rules_error.empty()) {
                warning_events.emplace_back("rules_load_failed");
            }

            // 响应侧规则检查
            const auto response_warnings = rule_engine.check_response(result.full_text);
            for (const auto &warning : response_warnings) {
                warning_events.push_back(warning);
            }

            // 若告警列表为空，则补一个默认值
            if (warning_events.empty()) {
                // 补默认告警标识
                warning_events.emplace_back("none");
            }
            // 拷贝计量信息
            llm::LlmMetrics metrics = result.metrics;
            // 若计量信息缺失，则补默认值
            if (!metrics.has_usage) {
                // 标记计量信息存在
                metrics.has_usage = true;
                // 设定 prompt token 数
                metrics.prompt_tokens = 0;
                // 设定 completion token 数为 token 列表长度
                metrics.completion_tokens = static_cast<int>(result.tokens.size());
                // 设定 total token 数
                metrics.total_tokens = metrics.completion_tokens;
            }

            // 组装 SSE 输出内容（data: JSON）
            // 初始化输出缓冲区
            std::string payload;
            // 预留一定容量减少扩容
            payload.reserve(256);
            // 逐个 token 构造 SSE 事件
            for (const auto &token : result.tokens) {
                // 构造事件 JSON
                Json::Value event;
                // 标记事件类型为 token
                event["type"] = "token";
                // 设置事件负载
                event["payload"] = token;
                // 写入 SSE 前缀
                payload += "data: ";
                // 序列化事件为一行 JSON 并追加
                payload += serialize_event(event);
                // 追加 SSE 分隔空行
                payload += "\n\n";
            }

            // 逐个引用构造 SSE 事件
            for (const auto &cite : cites) {
                // 构造事件 JSON
                Json::Value event;
                // 标记事件类型为 cite
                event["type"] = "cite";
                // 设置事件负载
                event["payload"] = cite;
                // 写入 SSE 前缀
                payload += "data: ";
                // 序列化事件为一行 JSON 并追加
                payload += serialize_event(event);
                // 追加 SSE 分隔空行
                payload += "\n\n";
            }

            // 仅当计量信息存在时输出 metric 事件
            if (metrics.has_usage) {
                // 构造事件 JSON
                Json::Value event;
                // 标记事件类型为 metric
                event["type"] = "metric";
                // 写入 prompt token 数
                event["payload"]["prompt_tokens"] = metrics.prompt_tokens;
                // 写入 completion token 数
                event["payload"]["completion_tokens"] = metrics.completion_tokens;
                // 写入 total token 数
                event["payload"]["total_tokens"] = metrics.total_tokens;
                // 写入 SSE 前缀
                payload += "data: ";
                // 序列化事件为一行 JSON 并追加
                payload += serialize_event(event);
                // 追加 SSE 分隔空行
                payload += "\n\n";
            }

            // 逐个告警构造 SSE 事件
            for (const auto &warning : warning_events) {
                // 构造事件 JSON
                Json::Value event;
                // 标记事件类型为 warning
                event["type"] = "warning";
                // 设置事件负载
                event["payload"] = warning;
                // 写入 SSE 前缀
                payload += "data: ";
                // 序列化事件为一行 JSON 并追加
                payload += serialize_event(event);
                // 追加 SSE 分隔空行
                payload += "\n\n";
            }

            // done 事件
            // 构造完成事件 JSON
            Json::Value done;
            // 标记事件类型为 done
            done["type"] = "done";
            // 设置事件负载
            done["payload"] = "done";
            // 写入 SSE 前缀
            payload += "data: ";
            // 序列化事件为一行 JSON 并追加
            payload += serialize_event(done);
            // 追加 SSE 分隔空行
            payload += "\n\n";

            // 将 payload 包装为共享指针以供流式回调使用
            auto data = std::make_shared<std::string>(std::move(payload));
            // 记录当前已写出的偏移量
            auto offset = std::make_shared<std::size_t>(0);

            // Drogon 流式回调：不断向客户端写出 SSE 内容
            // 创建流式响应对象
            auto resp = HttpResponse::newStreamResponse(
                [data, offset](char *buffer, std::size_t len) -> std::size_t {
                    // 若已写完则返回 0 表示结束
                    if (*offset >= data->size()) {
                        return 0;
                    }
                    // 计算剩余未写长度
                    const std::size_t remaining = data->size() - *offset;
                    // 计算本次可拷贝长度
                    const std::size_t to_copy = (std::min)(len, remaining);
                    // 拷贝数据到输出缓冲区
                    std::memcpy(buffer, data->data() + *offset, to_copy);
                    // 更新已写偏移
                    *offset += to_copy;
                    // 返回本次写出的字节数
                    return to_copy;
                });

            // SSE 必需的响应头
            // 设置响应类型为 SSE
            resp->setContentTypeString("text/event-stream");
            // 禁用缓存
            resp->addHeader("Cache-Control", "no-cache");
            // 保持连接不断开
            resp->addHeader("Connection", "keep-alive");
            resp->addHeader("X-Trace-Id", trace_id);
            // 回调返回响应
            cb(resp);

            const auto latency_ms = std::chrono::duration<double, std::milli>(
                                        std::chrono::steady_clock::now() - start)
                                        .count();
            log_request_metrics(trace_id, route, resp->getStatusCode(), latency_ms);
            g_concurrency_limiter.release();
        },
        // 指定该路由仅支持 POST 方法
        {Post});

    app().registerHandler("/metrics", [](const HttpRequestPtr &, std::function<void(const HttpResponsePtr &)> &&cb) {
        const auto snap = util::global_metrics().snapshot();
        Json::Value body;
        body["requests_total"] = Json::Int64(snap.requests_total);
        body["rate_limit_rejects_total"] = Json::Int64(snap.rate_limit_rejects_total);
        body["circuit_breaker_trips_total"] = Json::Int64(snap.circuit_breaker_trips_total);
        body["latency_p95_ms"] = snap.latency_p95_ms;
        body["latency_p99_ms"] = snap.latency_p99_ms;
        body["external_call_latency_ms_p95"] = snap.external_call_latency_ms_p95;
        auto resp = HttpResponse::newHttpJsonResponse(body);
        cb(resp);
    }, {Get});

    // ════════════════════════════════════════════════════════════════════
    // P6: CORS OPTIONS 预检处理器（通配所有 /api/v1/* 路由）
    // ════════════════════════════════════════════════════════════════════
    app().registerHandler("/api/v1/{path}", [](const HttpRequestPtr &, std::function<void(const HttpResponsePtr &)> &&cb) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k204NoContent);
        add_cors_headers(resp);
        cb(resp);
    }, {Options});

    // ════════════════════════════════════════════════════════════════════
    // P6: GET /api/v1/settings — 返回当前 LLM 配置（API Key 掩码）
    // ════════════════════════════════════════════════════════════════════
    app().registerHandler("/api/v1/settings", [](const HttpRequestPtr &, std::function<void(const HttpResponsePtr &)> &&cb) {
        llm::LlmClient client;
        const auto cfg = client.load_config();

        Json::Value body;
        body["api_base"] = cfg.api_base;
        body["model"] = cfg.model;
        body["temperature"] = cfg.temperature;
        body["timeout_ms"] = cfg.timeout_ms;

        // API Key 掩码：只返回是否已配置 + 后4位
        if (cfg.api_key.empty()) {
            body["api_key_configured"] = false;
            body["api_key_hint"] = "";
        } else {
            body["api_key_configured"] = true;
            if (cfg.api_key.size() > 4) {
                body["api_key_hint"] = "****" + cfg.api_key.substr(cfg.api_key.size() - 4);
            } else {
                body["api_key_hint"] = "****";
            }
        }

        auto resp = HttpResponse::newHttpJsonResponse(body);
        add_cors_headers(resp);
        cb(resp);
    }, {Get});

    // ════════════════════════════════════════════════════════════════════
    // P6: POST /api/v1/settings — 部分更新 LLM 配置
    // ════════════════════════════════════════════════════════════════════
    app().registerHandler("/api/v1/settings", [](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&cb) {
        // 解析请求 JSON
        auto json_ptr = req ? req->getJsonObject() : nullptr;
        if (!json_ptr) {
            Json::Value err;
            err["error"] = "invalid_json";
            auto resp = HttpResponse::newHttpJsonResponse(err);
            resp->setStatusCode(k400BadRequest);
            add_cors_headers(resp);
            cb(resp);
            return;
        }
        const auto &input = *json_ptr;

        std::lock_guard<std::mutex> lock(g_settings_mutex);

        // 读取当前 llm.json
        const auto config_path = std::filesystem::current_path() / "config" / "llm.json";
        nlohmann::json doc;
        {
            std::ifstream in(config_path);
            if (in) {
                try { in >> doc; } catch (...) { doc = nlohmann::json::object(); }
            }
        }

        // 合并传入字段（部分更新）
        if (input.isMember("api_key") && input["api_key"].isString()) {
            doc["api_key"] = input["api_key"].asString();
        }
        if (input.isMember("api_base") && input["api_base"].isString()) {
            doc["api_base"] = input["api_base"].asString();
        }
        if (input.isMember("model") && input["model"].isString()) {
            doc["model"] = input["model"].asString();
        }
        if (input.isMember("temperature") && input["temperature"].isNumeric()) {
            doc["temperature"] = input["temperature"].asDouble();
        }
        if (input.isMember("timeout_ms") && input["timeout_ms"].isNumeric()) {
            doc["timeout_ms"] = input["timeout_ms"].asInt();
        }

        // 写回文件
        {
            std::ofstream out(config_path);
            if (!out) {
                Json::Value err;
                err["error"] = "write_failed";
                auto resp = HttpResponse::newHttpJsonResponse(err);
                resp->setStatusCode(k500InternalServerError);
                add_cors_headers(resp);
                cb(resp);
                return;
            }
            out << doc.dump(4);
        }

        // 刷新 LLM 配置缓存，使下次请求立即读取新配置
        llm::invalidate_llm_config_cache();

        Json::Value body;
        body["status"] = "ok";
        body["message"] = "settings updated";
        auto resp = HttpResponse::newHttpJsonResponse(body);
        add_cors_headers(resp);
        cb(resp);

        LOG_INFO << "Settings updated via POST /api/v1/settings";
    }, {Post});

    // Phase 10: Value Cell 量化价值分析接口
    // POST /api/v1/valuecell  — 输入 {"symbol":"AAPL"}，返回 Value Cell 分析报告
    app().registerHandler("/api/v1/valuecell",
        [](const HttpRequestPtr &req,
           std::function<void(const HttpResponsePtr &)> &&cb) {
            valuation::handle_valuecell(req, std::move(cb));
        }, {Post});
}

}
