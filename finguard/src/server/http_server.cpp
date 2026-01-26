#include "server/http_server.h"
#include "server/routes.h"

#include <drogon/drogon.h>

namespace finguard {

void start_http_server() {
    // 注册全部 HTTP 路由
    setup_routes();

    // 启动 Drogon 服务器
    drogon::app()
        .setThreadNum(4)
        .addListener("0.0.0.0", 8080)
        .run();
}

}
