#include "server/routes_internal.h"

#include <drogon/drogon.h>

namespace finguard::server::internal {

void register_health_routes() {
    using namespace drogon;

    app().registerHandler("/health",
                          [](const HttpRequestPtr &,
                             std::function<void(const HttpResponsePtr &)> &&cb) {
                              Json::Value body;
                              body["status"] = "ok";
                              auto resp = HttpResponse::newHttpJsonResponse(body);
                              cb(resp);
                          },
                          {Get});
}

} // namespace finguard::server::internal
