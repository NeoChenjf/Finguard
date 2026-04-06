#include "server/routes_internal.h"

#include <drogon/drogon.h>

#include "valuation/valuation_handler.h"

namespace finguard::server::internal {

void register_valuecell_routes() {
    using namespace drogon;

    app().registerHandler(
        "/api/v1/valuecell",
        [](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&cb) {
            valuation::handle_valuecell(req, std::move(cb));
        },
        {Post});
}

} // namespace finguard::server::internal
