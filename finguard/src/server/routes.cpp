#include "server/routes.h"

#include "server/routes_internal.h"

namespace finguard {

void setup_routes() {
    server::internal::register_health_routes();
    server::internal::register_plan_routes();
    server::internal::register_profile_routes();
    server::internal::register_chat_routes();
    server::internal::register_system_routes();
    server::internal::register_valuecell_routes();
}

} // namespace finguard
