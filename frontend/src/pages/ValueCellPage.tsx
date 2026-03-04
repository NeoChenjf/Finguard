import { useState } from 'react';
import { Search, Loader2, TrendingUp, AlertTriangle, CheckCircle2, XCircle, Info, Clock } from 'lucide-react';
import { postValueCell, type ValueCellResponse } from '../api/client';

/** 格式化数字，负值显示 N/A */
function fmt(v: number, digits = 2): string {
  return v < 0 ? 'N/A' : v.toFixed(digits);
}

/** 指标卡片 */
function MetricCard({ label, value, sub, accent }: {
  label: string;
  value: string;
  sub?: string;
  accent?: 'green' | 'red' | 'amber' | 'blue';
}) {
  const accentColor = {
    green: 'border-green-600/40 bg-green-900/10',
    red: 'border-red-600/40 bg-red-900/10',
    amber: 'border-amber-600/40 bg-amber-900/10',
    blue: 'border-blue-600/40 bg-blue-900/10',
  }[accent ?? 'blue'] ?? 'border-gray-700 bg-gray-800/40';

  return (
    <div className={`rounded-xl border p-4 ${accentColor}`}>
      <div className="text-xs text-gray-400 mb-1">{label}</div>
      <div className="text-xl font-bold text-gray-100">{value}</div>
      {sub && <div className="text-xs text-gray-500 mt-1">{sub}</div>}
    </div>
  );
}

/** 警告条 */
function WarningBar({ label, text }: { label: string; text: string }) {
  return (
    <div className="flex items-start gap-2 px-3 py-2 bg-amber-900/15 border border-amber-800/40 rounded-lg text-xs text-amber-300">
      <AlertTriangle className="w-3.5 h-3.5 mt-0.5 flex-shrink-0" />
      <span><strong>{label}：</strong>{text}</span>
    </div>
  );
}

export default function ValueCellPage() {
  const [symbol, setSymbol] = useState('');
  const [loading, setLoading] = useState(false);
  const [result, setResult] = useState<ValueCellResponse | null>(null);
  const [error, setError] = useState('');

  // 常用快捷股票
  const quickSymbols = ['AAPL', 'MSFT', 'GOOGL', 'TSLA', 'BRK-B', 'JNJ'];

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

  return (
    <div className="h-full overflow-y-auto px-6 py-6 space-y-6">
      {/* 标题区 */}
      <div>
        <h1 className="text-xl font-bold flex items-center gap-2 text-gray-100">
          <TrendingUp className="w-5 h-5 text-blue-400" />
          Value Cell 量化价值分析
        </h1>
        <p className="text-sm text-gray-500 mt-1">
          输入美股代码，自动获取财务指标、计算安全边际、生成定性分析报告
        </p>
      </div>

      {/* 搜索区 */}
      <div className="flex items-center gap-3">
        <div className="relative flex-1 max-w-md">
          <Search className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-gray-500" />
          <input
            type="text"
            value={symbol}
            onChange={(e) => setSymbol(e.target.value.toUpperCase())}
            onKeyDown={handleKeyDown}
            placeholder="输入股票代码，如 AAPL"
            className="w-full bg-gray-800 border border-gray-700 rounded-xl pl-10 pr-4 py-2.5 text-sm text-gray-100 placeholder-gray-600 focus:outline-none focus:border-blue-500 focus:ring-1 focus:ring-blue-500"
          />
        </div>
        <button
          onClick={() => handleAnalyze()}
          disabled={!symbol.trim() || loading}
          className="px-5 py-2.5 bg-blue-600 hover:bg-blue-500 disabled:bg-gray-700 disabled:text-gray-500 text-white text-sm font-medium rounded-xl transition-colors cursor-pointer flex items-center gap-2"
        >
          {loading ? (
            <>
              <Loader2 className="w-4 h-4 animate-spin" />
              分析中…
            </>
          ) : (
            '开始分析'
          )}
        </button>
      </div>

      {/* 快捷入口 */}
      <div className="flex flex-wrap gap-2">
        {quickSymbols.map((s) => (
          <button
            key={s}
            onClick={() => handleAnalyze(s)}
            disabled={loading}
            className="px-3 py-1.5 text-xs bg-gray-800 hover:bg-gray-700 border border-gray-700 text-gray-300 rounded-lg transition-colors cursor-pointer disabled:opacity-40"
          >
            {s}
          </button>
        ))}
      </div>

      {/* 错误提示 */}
      {error && (
        <div className="flex items-center gap-2 px-4 py-3 bg-red-900/20 border border-red-800/50 rounded-xl text-sm text-red-300">
          <XCircle className="w-4 h-4 flex-shrink-0" />
          {error}
        </div>
      )}

      {/* 加载动画 */}
      {loading && (
        <div className="flex flex-col items-center justify-center py-16 text-gray-500 space-y-3">
          <Loader2 className="w-10 h-10 animate-spin text-blue-400" />
          <p className="text-sm">正在获取 {symbol} 数据并分析，请稍候…</p>
          <p className="text-xs text-gray-600">通常需要 10–30 秒（含 Yahoo 数据 + LLM 分析）</p>
        </div>
      )}

      {/* 结果展示 */}
      {result && !loading && (() => {
        /* 数据不足时：定性分析 & 完整报告优先，指标和安全边际放最后 */
        const dataInsufficient = result.current_pe < 0 && result.current_peg < 0;

        const warningsBlock = (
          <div className="space-y-2" key="warn">
            {result.data_warning && <WarningBar label="数据" text={result.data_warning} />}
            {result.search_warning && <WarningBar label="搜索" text={result.search_warning} />}
            {result.llm_warning && <WarningBar label="LLM" text={result.llm_warning} />}
          </div>
        );

        const coreMetrics = (
          <div className="grid grid-cols-2 md:grid-cols-4 gap-4" key="core">
            <MetricCard
              label="安全边际"
              value={result.safety_margin ? '✅ 是' : '❌ 否'}
              sub={result.safety_margin_reason.slice(0, 50)}
              accent={result.safety_margin ? 'green' : 'red'}
            />
            <MetricCard
              label="定性评分"
              value={result.qualitative_score >= 0 ? `${fmt(result.qualitative_score, 1)} / 10` : 'N/A'}
              accent={result.qualitative_score >= 7 ? 'green' : result.qualitative_score >= 4 ? 'amber' : 'red'}
            />
            <MetricCard
              label="当前 PE"
              value={fmt(result.current_pe)}
              sub={`历史均值 ${fmt(result.historical_pe_mean)}`}
              accent={result.current_pe > 0 && result.historical_pe_mean > 0 && result.current_pe < result.historical_pe_mean ? 'green' : 'amber'}
            />
            <MetricCard
              label="PEG"
              value={fmt(result.current_peg)}
              sub={result.current_peg > 0 && result.current_peg < 1 ? '< 1 低估信号' : result.current_peg >= 1 ? '≥ 1' : ''}
              accent={result.current_peg > 0 && result.current_peg < 1 ? 'green' : 'amber'}
            />
          </div>
        );

        const detailMetrics = (
          <div className="grid grid-cols-2 md:grid-cols-4 gap-4" key="detail">
            <MetricCard label="市净率 (PB)" value={fmt(result.price_to_book)} />
            <MetricCard label="盈利增速" value={result.earnings_growth !== 0 ? `${(result.earnings_growth * 100).toFixed(1)}%` : 'N/A'} />
            <MetricCard
              label="数据来源"
              value={result.data_source === 'yahoo_finance' ? 'Yahoo' : result.data_source === 'alpha_vantage' ? 'AlphaVantage' : result.data_source}
              accent="blue"
            />
            <MetricCard
              label="分析耗时"
              value={`${(result.analysis_time_ms / 1000).toFixed(1)}s`}
              accent="blue"
            />
          </div>
        );

        const safetyMarginBlock = (
          <section className="bg-gray-800/50 border border-gray-700 rounded-xl p-5" key="safety">
            <h2 className="text-sm font-semibold text-gray-200 flex items-center gap-2 mb-3">
              {result.safety_margin ? (
                <CheckCircle2 className="w-4 h-4 text-green-400" />
              ) : (
                <XCircle className="w-4 h-4 text-red-400" />
              )}
              安全边际分析
            </h2>
            <p className="text-sm text-gray-300 leading-relaxed whitespace-pre-wrap">
              {result.safety_margin_reason}
            </p>
          </section>
        );

        const qualitativeBlock = (result.qualitative_analysis.moat || result.qualitative_analysis.management || result.qualitative_analysis.business_model) ? (
          <section className="bg-gray-800/50 border border-gray-700 rounded-xl p-5 space-y-3" key="qual">
            <h2 className="text-sm font-semibold text-gray-200 flex items-center gap-2">
              <Info className="w-4 h-4 text-blue-400" />
              定性分析
            </h2>
            {result.qualitative_analysis.moat && (
              <div>
                <span className="text-xs font-medium text-blue-300">护城河</span>
                <p className="text-sm text-gray-300 mt-0.5">{result.qualitative_analysis.moat}</p>
              </div>
            )}
            {result.qualitative_analysis.management && (
              <div>
                <span className="text-xs font-medium text-blue-300">管理层</span>
                <p className="text-sm text-gray-300 mt-0.5">{result.qualitative_analysis.management}</p>
              </div>
            )}
            {result.qualitative_analysis.business_model && (
              <div>
                <span className="text-xs font-medium text-blue-300">商业模式</span>
                <p className="text-sm text-gray-300 mt-0.5">{result.qualitative_analysis.business_model}</p>
              </div>
            )}
          </section>
        ) : null;

        const reportBlock = (
          <section className="bg-gray-800/50 border border-gray-700 rounded-xl p-5" key="report">
            <h2 className="text-sm font-semibold text-gray-200 flex items-center gap-2 mb-3">
              <TrendingUp className="w-4 h-4 text-blue-400" />
              完整分析报告
            </h2>
            <div className="prose prose-invert prose-sm max-w-none text-gray-300 leading-relaxed whitespace-pre-wrap">
              {result.investment_conclusion}
            </div>
          </section>
        );

        const metaBlock = (
          <div className="flex items-center gap-4 text-xs text-gray-600 pb-4" key="meta">
            <span className="flex items-center gap-1">
              <Clock className="w-3 h-3" />
              分析于 {new Date().toLocaleString('zh-CN')}
            </span>
            <span>PE 历史季度数: {result.pe_history_quarters_used}</span>
            <span>数据源: {result.data_source}</span>
          </div>
        );

        return (
          <div className="space-y-6">
            {warningsBlock}
            {dataInsufficient ? (
              <>
                {qualitativeBlock}
                {reportBlock}
                {coreMetrics}
                {detailMetrics}
                {safetyMarginBlock}
              </>
            ) : (
              <>
                {coreMetrics}
                {detailMetrics}
                {safetyMarginBlock}
                {qualitativeBlock}
                {reportBlock}
              </>
            )}
            {metaBlock}
          </div>
        );
      })()}

      {/* 空状态 */}
      {!result && !loading && !error && (
        <div className="flex flex-col items-center justify-center py-20 text-gray-600 space-y-3">
          <div className="w-16 h-16 rounded-2xl bg-gray-800 flex items-center justify-center">
            <TrendingUp className="w-7 h-7 text-gray-600" />
          </div>
          <p className="text-sm">输入股票代码开始价值分析</p>
          <p className="text-xs text-gray-700">基于 PE / PEG / 安全边际 + LLM 定性评分</p>
        </div>
      )}
    </div>
  );
}
