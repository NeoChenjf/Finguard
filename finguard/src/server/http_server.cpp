#include "server/http_server.h"
#include "server/routes.h"

#include <drogon/drogon.h>

namespace finguard {

void start_http_server() {
    // 注册全部 HTTP 路由
    setup_routes();

    // P6/P9: 全局 CORS 预路由拦截 —— 在路由匹配之前处理所有 OPTIONS 预检请求
    // 必须使用 registerPreRoutingAdvice（而非 PreHandling），因为 Drogon 在路由
    // 匹配阶段就会对方法不匹配的请求返回 403，PreHandling 来不及拦截。
    drogon::app().registerPreRoutingAdvice(
        [](const drogon::HttpRequestPtr &req,
           std::function<void(const drogon::HttpResponsePtr &)> &&done,
           std::function<void()> &&pass) {
            if (req->method() == drogon::Options) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k204NoContent);
                resp->addHeader("Access-Control-Allow-Origin", "*");
                resp->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
                resp->addHeader("Access-Control-Allow-Headers",
                                "Content-Type, X-User-Id, X-Trace-Id, X-Api-Key");
                resp->addHeader("Access-Control-Max-Age", "86400");
                done(resp);
                return;
            }
            pass();
        });

    // P6: 全局 CORS 后处理（为所有响应添加 CORS 头）
    drogon::app().registerPostHandlingAdvice(
        [](const drogon::HttpRequestPtr &req, const drogon::HttpResponsePtr &resp) {
            resp->addHeader("Access-Control-Allow-Origin", "*");
            resp->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
            resp->addHeader("Access-Control-Allow-Headers",
                            "Content-Type, X-User-Id, X-Trace-Id, X-Api-Key");
        });

    // 启动 Drogon 服务器
    drogon::app()
        .setThreadNum(4)
        .addListener("0.0.0.0", 8080)
        .run();
}

}
