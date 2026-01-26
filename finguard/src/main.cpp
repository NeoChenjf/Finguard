// 程序入口：只负责启动 HTTP 服务
#include "server/http_server.h"

int main() {
    // 启动 Drogon HTTP 服务
    finguard::start_http_server();
    return 0;
}
