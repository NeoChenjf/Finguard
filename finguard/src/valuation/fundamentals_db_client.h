#pragma once

#include "valuation/yahoo_finance_client.h"

#include <string>

namespace finguard::valuation {

FinancialMetrics fetch_financial_metrics_from_db(
    const std::string &requested_symbol,
    const std::string &configured_profile = "main");

} // namespace finguard::valuation
