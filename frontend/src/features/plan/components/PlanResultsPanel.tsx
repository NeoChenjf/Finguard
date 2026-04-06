import { AlertCircle, AlertTriangle, CheckCircle2 } from 'lucide-react';

import { RULE_OPTIONS } from '../constants';
import type { usePlanPage } from '../usePlanPage';

type PlanPageState = ReturnType<typeof usePlanPage>;

interface PlanResultsPanelProps {
  state: PlanPageState;
}

function getStatusBadge(status: string) {
  if (status === 'PASS') {
    return (
      <span className="flex items-center gap-1 px-2.5 py-1 bg-green-900/30 border border-green-800 rounded-full text-xs text-green-300">
        <CheckCircle2 className="w-3.5 h-3.5" /> PASS
      </span>
    );
  }
  if (status === 'WARN') {
    return (
      <span className="flex items-center gap-1 px-2.5 py-1 bg-amber-900/30 border border-amber-800 rounded-full text-xs text-amber-300">
        <AlertTriangle className="w-3.5 h-3.5" /> WARN
      </span>
    );
  }
  return (
    <span className="flex items-center gap-1 px-2.5 py-1 bg-red-900/30 border border-red-800 rounded-full text-xs text-red-300">
      <AlertCircle className="w-3.5 h-3.5" /> FAIL
    </span>
  );
}

export function PlanResultsPanel({ state }: PlanResultsPanelProps) {
  return (
    <div className="space-y-6">
      {state.error && (
        <div className="bg-red-900/30 border border-red-800 rounded-xl p-4 flex items-start gap-2 text-sm text-red-300">
          <AlertCircle className="w-4 h-4 mt-0.5 flex-shrink-0" />
          {state.error}
        </div>
      )}

      {state.hasResults && (
        <>
          <section className="bg-gray-900 rounded-xl p-5 border border-gray-800">
            <h2 className="text-sm font-semibold text-gray-300 mb-4">多规则配置对比</h2>
            <div className="overflow-x-auto">
              <table className="w-full text-xs text-gray-300 border-collapse">
                <thead>
                  <tr className="border-b border-gray-800">
                    <th className="text-left py-2 pr-3">资产</th>
                    {state.activeResults.map(({ rule }) => (
                      <th key={rule} className="text-right py-2 px-2 font-medium">
                        {RULE_OPTIONS.find((item) => item.value === rule)?.label ?? rule}
                      </th>
                    ))}
                  </tr>
                </thead>
                <tbody>
                  {state.compareSymbols.map((symbol) => (
                    <tr key={symbol} className="border-b border-gray-900/50">
                      <td className="py-2 pr-3 font-mono text-gray-400">{symbol}</td>
                      {state.activeResults.map(({ rule, data }) => (
                        <td key={`${rule}-${symbol}`} className="text-right py-2 px-2">
                          {(((data.proposed_portfolio[symbol] ?? 0) as number) * 100).toFixed(1)}%
                        </td>
                      ))}
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          </section>

          {state.activeResults.map(({ rule, data }) => (
            <section key={rule} className="bg-gray-900 rounded-xl p-5 border border-gray-800">
              <div className="flex items-center justify-between mb-3">
                <h2 className="text-sm font-semibold text-gray-300">
                  {RULE_OPTIONS.find((item) => item.value === rule)?.label ?? rule}
                </h2>
                {getStatusBadge(data.risk_report.status)}
              </div>

              <p className="text-sm text-gray-400 leading-relaxed mb-3">{data.rationale}</p>

              {data.risk_report.triggered_rules.length > 0 && (
                <ul className="text-xs text-gray-400 space-y-1 list-disc list-inside mb-3">
                  {data.risk_report.triggered_rules.map((triggeredRule, index) => (
                    <li key={index}>{triggeredRule}</li>
                  ))}
                </ul>
              )}

              {data.rebalancing_actions.length > 0 && (
                <ul className="text-sm text-gray-400 space-y-1.5">
                  {data.rebalancing_actions.map((action, index) => (
                    <li key={index} className="flex items-start gap-2">
                      <span className="text-blue-400 font-mono text-xs mt-0.5">{index + 1}.</span>
                      {typeof action === 'string'
                        ? action
                        : `${action.action} ${action.symbol}：${(action.current_weight * 100).toFixed(1)}% → ${(action.target_weight * 100).toFixed(1)}%`}
                    </li>
                  ))}
                </ul>
              )}
            </section>
          ))}
        </>
      )}

      {!state.hasResults && !state.error && (
        <div className="flex items-center justify-center h-64 text-gray-600 text-sm">
          填写左侧画像并选择规则，点击"生成对比方案"查看结果
        </div>
      )}
    </div>
  );
}
