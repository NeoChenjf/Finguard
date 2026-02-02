// P1 诊断: 测试 trantor/Drogon 的 DNS 解析是否正常
#include <iostream>
#include <drogon/drogon.h>
#include <trantor/net/Resolver.h>
#include <trantor/net/EventLoop.h>

int main() {
    std::cout << "[P1 DNS Diagnostic]" << std::endl;
    std::cout << "c-ares used: " << (trantor::Resolver::isCAresUsed() ? "YES" : "NO") << std::endl;
    
    trantor::EventLoop loop;
    
    auto resolver = trantor::Resolver::newResolver(&loop, 10);
    
    std::string target = "dashscope.aliyuncs.com";
    std::cout << "Resolving: " << target << std::endl;
    
    bool resolved = false;
    resolver->resolve(target, [&](const std::vector<trantor::InetAddress>& addrs) {
        std::cout << "Resolved addresses count: " << addrs.size() << std::endl;
        for (const auto& addr : addrs) {
            std::cout << "  IP: " << addr.toIp() << " Port: " << addr.toPort() << std::endl;
        }
        if (addrs.empty()) {
            std::cout << "  [ERROR] No addresses resolved - this is the problem!" << std::endl;
        }
        resolved = true;
        loop.quit();
    });
    
    // 设置超时
    loop.runAfter(5.0, [&]() {
        if (!resolved) {
            std::cout << "[ERROR] DNS resolution timed out after 5 seconds!" << std::endl;
            loop.quit();
        }
    });
    
    loop.loop();
    
    return 0;
}
