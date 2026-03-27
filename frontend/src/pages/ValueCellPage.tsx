import { useState } from 'react';
import {
  Search,
  Loader2,
  TrendingUp,
  AlertTriangle,
  XCircle,
  Clock,
  Star,
} from 'lucide-react';
import { postValueCell, type ValueCellResponse } from '../api/client';

type RuleStatus = 'pass' | 'observe' | 'fail' | 'na';

function fmtNumber(v: number, digits = 2): string {
  return typeof v !== 'number' || Number.isNaN(v) || v < 0 ? 'N/A' : v.toFixed(digits);
}

function fmtPercent(v: number, digits = 2): string {
  return typeof v !== 'number' || Number.isNaN(v) || v < 0 ? 'N/A' : `${(v * 100).toFixed(digits)}%`;
}

function formatWarning(code: string): string {
  const map: Record<string, string> = {
    invalid_eps: 'EPS 小于等于 0，无法计算 PE / PEG。',
    insufficient_net_profit_history: '净利润历史不足 8 年，增长率口径退化或不可完整解释。',
    growth_fallback_avg_yoy_used: '由于净利润历史不足 8 年，growth 采用平均同比增速降级计算。',
    invalid_growth_base: '历史净利润基数无效，无法计算 5 年 CAGR。',
    non_positive_growth: '5 年年化增长率小于等于 0，PEG 不具解释性。',
    low_growth_5y_cagr: '5 年年化增长率低于 10%，需关注成长性不足风险。',
    high_debt_ratio: '负债率高于 50%，需关注偿债压力和财务稳健性。',
    missing_debt_ratio: '缺少总负债或总资产，无法计算负债率。',
    missing_roe_history: '股东权益历史不足 5 年，无法计算 5 年平均 ROE。',
    yahoo_quotesummary_http_403: 'Yahoo quoteSummary 返回 403，已进入降级链。',
    yahoo_quotesummary_network_error: 'Yahoo quoteSummary 网络失败，已进入降级链。',
    yahoo_net_income_http_error: 'Yahoo 净利润历史请求失败。',
    yahoo_balance_sheet_http_error: 'Yahoo 资产负债表请求失败。',
    alpha_balance_sheet_http_error: 'Alpha Vantage 资产负债表请求失败。',
    alpha_balance_sheet_error: 'Alpha Vantage 资产负债表返回错误。',
    alpha_vantage_rate_limited: 'Alpha Vantage 当日额度已用尽，当前请求无法继续从该源补全数据。',
    alpha_vantage_error: 'Alpha Vantage 返回错误，当前请求无法从该源补全数据。',
    yahoo_crumb_unavailable: 'Yahoo 当前无法建立会话，系统已跳过 Yahoo 并尝试降级。',
    finnhub_quote_http_error: 'Finnhub 行情接口请求失败。',
    finnhub_quote_parse_error: 'Finnhub 行情接口返回内容无法解析。',
    finnhub_metric_http_error: 'Finnhub 指标接口请求失败。',
    finnhub_metric_error: 'Finnhub 指标接口返回错误。',
    finnhub_metric_cache_fallback: 'Finnhub 指标接口本次未完整返回，系统已回退到最近一次成功缓存补齐主字段。',
    finnhub_quote_and_metric_parse_error: 'Finnhub 行情与指标返回内容均无法解析。',
    finnhub_metric_empty: 'Finnhub 未返回可用的 PE/PB/ROE/价格指标。',
    finnhub_balance_sheet_http_error: 'Finnhub 资产负债表接口请求失败。',
    finnhub_balance_sheet_error: 'Finnhub 资产负债表接口返回错误。',
    finnhub_balance_sheet_empty: 'Finnhub 未返回可用资产负债表数据。',
    finnhub_balance_sheet_incomplete: 'Finnhub 资产负债表字段不完整，无法计算负债率。',
    finnhub_financials_http_error: 'Finnhub 财报历史请求失败。',
    finnhub_financials_error: 'Finnhub 财报历史返回错误。',
    finnhub_financials_no_data: 'Finnhub 未返回财报历史数据。',
    finnhub_financials_empty: 'Finnhub 财报历史为空。',
    finnhub_rate_limited: 'Finnhub 当前触发频率限制，请稍后重试。',
    quote_from_sample_cache: '当前行情主字段已从本地样本缓存补齐，用于避免页面出现大面积 N/A。',
    debt_ratio_from_sample_cache: '当前负债率已从本地样本缓存回填，用于避免页面出现缺失。',
  };

  return map[code] ?? code;
}

function getRuleStatus(result: ValueCellResponse, key: 'roe' | 'peg' | 'debt' | 'pepb' | 'cagr'): RuleStatus {
  switch (key) {
    case 'roe':
      if (result.avg_roe_5y < 0) return 'na';
      return result.avg_roe_5y > 0.2 ? 'pass' : 'fail';
    case 'peg':
      if (result.peg < 0) return 'na';
      if (result.peg < 1) return 'pass';
      if (result.peg <= 2) return 'observe';
      return 'fail';
    case 'debt':
      if (result.debt_ratio < 0) return 'na';
      return result.debt_ratio <= 0.5 ? 'pass' : 'fail';
    case 'pepb': {
      if (result.pe < 0 || result.price_to_book < 0) return 'na';
      return result.pe * result.price_to_book < 2.25 ? 'pass' : 'fail';
    }
    case 'cagr':
      if (result.growth_5y_cagr < 0) return 'na';
      return result.growth_5y_cagr > 0.2 ? 'pass' : 'fail';
  }
}

function getRuleMeta(status: RuleStatus) {
  switch (status) {
    case 'pass':
      return {
        text: '通过',
        box: 'border-green-700/50 bg-green-900/15 text-green-300',
        dot: 'bg-green-400',
      };
    case 'observe':
      return {
        text: '正常',
        box: 'border-amber-700/50 bg-amber-900/15 text-amber-300',
        dot: 'bg-amber-400',
      };
    case 'fail':
      return {
        text: '未通过',
        box: 'border-red-700/50 bg-red-900/15 text-red-300',
        dot: 'bg-red-400',
      };
    default:
      return {
        text: 'N/A',
        box: 'border-slate-700/50 bg-slate-800 text-slate-300',
        dot: 'bg-slate-400',
      };
  }
}

function MetricCard({
  label,
  value,
  sub,
}: {
  label: string;
  value: string;
  sub?: string;
}) {
  return (
    <div className="rounded-xl border border-gray-700 bg-gray-800/50 p-4">
      <div className="text-xs text-gray-500">{label}</div>
      <div className="mt-1 text-xl font-semibold text-gray-100">{value}</div>
      {sub && <div className="mt-1 text-xs text-gray-500">{sub}</div>}
    </div>
  );
}

function WarningBar({ text }: { text: string }) {
  return (
    <div className="flex items-start gap-2 rounded-lg border border-amber-800/40 bg-amber-900/15 px-3 py-2 text-xs text-amber-300">
      <AlertTriangle className="mt-0.5 h-3.5 w-3.5 flex-shrink-0" />
      <span>{text}</span>
    </div>
  );
}

export default function ValueCellPage() {
  const [symbol, setSymbol] = useState('');
  const [loading, setLoading] = useState(false);
  const [result, setResult] = useState<ValueCellResponse | null>(null);
  const [error, setError] = useState('');

  const quickSymbols = ['TCOM', 'PDD', 'BRK'];

  const handleAnalyze = async (sym?: string) => {
    const target = (sym ?? symbol).trim().toUpperCase();
    if (!target || loading) return;

    setSymbol(target);
    setLoading(true);
    setError('');
    setResult(null);

    try {
      const data = await postValueCell(target);
      setResult(data);
    } catch (e) {
      setError((e as Error).message || '分析请求失败');
    } finally {
      setLoading(false);
    }
  };

  const handleKeyDown = (e: React.KeyboardEvent) => {
    if (e.key === 'Enter') {
      e.preventDefault();
      handleAnalyze();
    }
  };

  const warnings = result && Array.isArray(result.warnings) ? result.warnings : [];

  const dashboardRules = result
    ? [
        {
          label: '最近五年平均 ROE > 20%',
          status: getRuleStatus(result, 'roe'),
          detail: `当前 ROE：${fmtPercent(result.avg_roe_5y)}`,
        },
        {
          label: 'PEG < 1 非常安全；1~2 正常；>2 充分价格反应',
          status: getRuleStatus(result, 'peg'),
          detail: `当前 PEG：${fmtNumber(result.peg)}`,
        },
        {
          label: '负债率 <= 50%',
          status: getRuleStatus(result, 'debt'),
          detail: `当前负债率：${fmtPercent(result.debt_ratio)}`,
        },
        {
          label: 'PE × PB < 2.25',
          status: getRuleStatus(result, 'pepb'),
          detail: result.pe >= 0 && result.price_to_book >= 0 ? `当前值：${(result.pe * result.price_to_book).toFixed(2)}` : '当前值：N/A',
        },
        {
          label: '5 年 CAGR > 20%',
          status: getRuleStatus(result, 'cagr'),
          detail: `当前 CAGR：${fmtPercent(result.growth_5y_cagr)}`,
        },
      ]
    : [];

  const passCount = dashboardRules.filter((item) => item.status === 'pass').length;

  return (
    <div className="h-full overflow-y-auto px-6 py-6 space-y-6">
      <div>
        <h1 className="flex items-center gap-2 text-xl font-bold text-gray-100">
          <TrendingUp className="h-5 w-5 text-blue-400" />
          Value Cell 基本面分析
        </h1>
        <p className="mt-1 text-sm text-gray-500">
          保留单仪表盘布局；删除完整分析报告；PEG / ROE / 负债率 / PE×PB / CAGR 五项组成仪表盘评分。
        </p>
      </div>

      <div className="flex items-center gap-3">
        <div className="relative max-w-md flex-1">
          <Search className="absolute left-3 top-1/2 h-4 w-4 -translate-y-1/2 text-gray-500" />
          <input
            type="text"
            value={symbol}
            onChange={(e) => setSymbol(e.target.value.toUpperCase())}
            onKeyDown={handleKeyDown}
            placeholder="输入股票代码，如 TCOM"
            className="w-full rounded-xl border border-gray-700 bg-gray-800 py-2.5 pr-4 pl-10 text-sm text-gray-100 placeholder-gray-600 focus:border-blue-500 focus:ring-1 focus:ring-blue-500 focus:outline-none"
          />
        </div>
        <button
          onClick={() => handleAnalyze()}
          disabled={!symbol.trim() || loading}
          className="flex cursor-pointer items-center gap-2 rounded-xl bg-blue-600 px-5 py-2.5 text-sm font-medium text-white transition-colors hover:bg-blue-500 disabled:bg-gray-700 disabled:text-gray-500"
        >
          {loading ? (
            <>
              <Loader2 className="h-4 w-4 animate-spin" />
              分析中…
            </>
          ) : (
            '开始分析'
          )}
        </button>
      </div>

      <div className="flex flex-wrap gap-2">
        {quickSymbols.map((s) => (
          <button
            key={s}
            onClick={() => handleAnalyze(s)}
            disabled={loading}
            className="cursor-pointer rounded-lg border border-gray-700 bg-gray-800 px-3 py-1.5 text-xs text-gray-300 transition-colors hover:bg-gray-700 disabled:opacity-40"
          >
            {s}
          </button>
        ))}
      </div>

      {error && (
        <div className="flex items-center gap-2 rounded-xl border border-red-800/50 bg-red-900/20 px-4 py-3 text-sm text-red-300">
          <XCircle className="h-4 w-4 flex-shrink-0" />
          {error}
        </div>
      )}

      {loading && (
        <div className="flex flex-col items-center justify-center space-y-3 py-16 text-gray-500">
          <Loader2 className="h-10 w-10 animate-spin text-blue-400" />
          <p className="text-sm">正在获取 {symbol} 数据并分析，请稍候…</p>
          <p className="text-xs text-gray-600">当前链路仍可能需要 10–20 秒。</p>
        </div>
      )}

      {result && !loading && (
        <div className="space-y-6">
          <section className="rounded-2xl border border-gray-700 bg-gray-800/50 p-5 space-y-5">
            <div className="flex flex-wrap items-center justify-between gap-4">
              <div>
                <h2 className="text-sm font-semibold text-gray-200">仪表盘</h2>
                <p className="mt-1 text-xs text-gray-500">单仪表盘展示当前价格、PE、PB、PEG、ROE、负债率、5年CAGR 与 5 星规则。</p>
              </div>
              <div className="rounded-xl border border-gray-700 bg-gray-900/40 px-4 py-3">
                <div className="text-xs text-gray-500">通过星级</div>
                <div className="mt-1 flex items-center gap-1 text-yellow-300">
                  {Array.from({ length: 5 }).map((_, i) => (
                    <Star key={i} className={`h-4 w-4 ${i < passCount ? 'fill-current text-green-400' : 'text-gray-600'}`} />
                  ))}
                  <span className="ml-2 text-sm text-gray-200">{passCount}/5</span>
                </div>
              </div>
            </div>

            <div className="grid grid-cols-2 gap-4 xl:grid-cols-4">
              <MetricCard label="当前股价" value={fmtNumber(result.price)} />
              <MetricCard label="当前 PE" value={fmtNumber(result.pe)} sub="优先使用上游 current PE" />
              <MetricCard label="PB" value={fmtNumber(result.price_to_book)} />
              <MetricCard label="PEG" value={fmtNumber(result.peg)} sub={result.peg < 0 ? 'N/A' : result.peg < 1 ? '非常安全' : result.peg <= 2 ? '正常' : '充分价格反应'} />
              <MetricCard label="5年平均 ROE" value={fmtPercent(result.avg_roe_5y)} />
              <MetricCard label="负债率" value={fmtPercent(result.debt_ratio)} />
              <MetricCard label="5年 CAGR" value={fmtPercent(result.growth_5y_cagr)} />
              <MetricCard label="估值结论" value={result.valuation_label || 'N/A'} />
            </div>

            <div className="space-y-3">
              {dashboardRules.map((item) => {
                const meta = getRuleMeta(item.status);
                const isGreen = item.status === 'pass';
                return (
                  <div key={item.label} className={`flex items-center justify-between gap-3 rounded-xl border px-4 py-3 ${meta.box}`}>
                    <div>
                      <div className={`text-sm font-medium ${isGreen ? 'text-green-300' : ''}`}>{item.label}</div>
                      <div className="mt-1 text-xs opacity-90">{item.detail}</div>
                    </div>
                    <div className="flex items-center gap-2">
                      <span className={`h-2.5 w-2.5 rounded-full ${meta.dot}`} />
                      <span className="text-xs font-medium">{meta.text}</span>
                    </div>
                  </div>
                );
              })}
            </div>

            <div className="space-y-3 border-t border-gray-700 pt-4">
              <h2 className="flex items-center gap-2 text-sm font-semibold text-gray-200">
                <AlertTriangle className="h-4 w-4 text-amber-400" />
                告警
              </h2>
              <div className="space-y-2">
              {result.data_warning && <WarningBar text={result.data_warning} />}
              {result.search_warning && <WarningBar text={result.search_warning} />}
              {result.llm_warning && <WarningBar text={result.llm_warning} />}
              {warnings.map((w, idx) => (
                <WarningBar key={`${w}-${idx}`} text={formatWarning(w)} />
              ))}
              {!result.data_warning && !result.search_warning && !result.llm_warning && warnings.length === 0 && (
                <div className="text-sm text-gray-400">无告警</div>
              )}
              </div>
            </div>
          </section>

          <div className="flex flex-wrap items-center gap-4 pb-4 text-xs text-gray-600">
            <span className="flex items-center gap-1">
              <Clock className="h-3 w-3" />
              分析于 {new Date().toLocaleString('zh-CN')}
            </span>
            <span>symbol: {result.symbol}</span>
            <span>数据源: {result.data_source || 'unknown'}</span>
          </div>
        </div>
      )}

      {!result && !loading && !error && (
        <div className="flex flex-col items-center justify-center space-y-3 py-20 text-gray-600">
          <div className="flex h-16 w-16 items-center justify-center rounded-2xl bg-gray-800">
            <TrendingUp className="h-7 w-7 text-gray-600" />
          </div>
          <p className="text-sm">输入股票代码开始价值分析</p>
          <p className="text-xs text-gray-700">保留单仪表盘，只展示必要指标和 5 星规则</p>
        </div>
      )}
    </div>
  );
}
