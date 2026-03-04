import { useState, useMemo } from 'react';
import { PieChart, Loader2, Plus, Trash2, AlertTriangle, CheckCircle2, AlertCircle, Info } from 'lucide-react';
import { postPlan, type PlanRequest, type PlanResponse } from '../api/client';

interface Holding {
  symbol: string;
  weight: number;
}

type InvestorType = 'novice' | 'experienced' | 'professional';

const defaultHoldings: Holding[] = [
  { symbol: 'VOO', weight: 0.48 },
  { symbol: '沪深300', weight: 0.09 },
  { symbol: '恒生指数', weight: 0.03 },
  { symbol: 'VGIT', weight: 0.30 },
  { symbol: 'GLD', weight: 0.10 },
];

/** 根据守拙理念计算建议配置（个股比例为占总配置的百分比） */
function computeAllocation(age: number, investorType: InvestorType, stockPickPctTotal: number) {
  const gold = 0.10;
  const bondDecade = Math.floor(age / 10) * 10;
  const bond = bondDecade / 100;
  const equity = Math.max(0, 1 - gold - bond);

  // 个股直接占总配置（不能超过股票部分）
  let individualAllowed = false;
  if (investorType === 'experienced' || investorType === 'professional') individualAllowed = true;
  const stockPick = individualAllowed ? Math.min(stockPickPctTotal, equity) : 0;
  const indexEquity = equity - stockPick;

  // 指数比例：港股:A股:美股 = 1:3:16
  const totalRatio = 1 + 3 + 16;
  const hk = indexEquity * (1 / totalRatio);
  const a = indexEquity * (3 / totalRatio);
  const us = indexEquity * (16 / totalRatio);

  return { gold, bond, equity, hk, a, us, stockPick, individualAllowed };
}

export default function PlanPage() {
  // ── 问卷画像 ──
  const [age, setAge] = useState(30);
  const [investorType, setInvestorType] = useState<InvestorType>('novice');
  const [experienceYears, setExperienceYears] = useState<string>('0-5');
  const [annualReturn, setAnnualReturn] = useState<string>('0-10');
  const [beatSP500, setBeatSP500] = useState<string>('no');
  const [stockPickPercent, setStockPickPercent] = useState(0);

  // ── 当前持仓 ──
  const [holdings, setHoldings] = useState<Holding[]>(defaultHoldings);
  const [loading, setLoading] = useState(false);
  const [result, setResult] = useState<PlanResponse | null>(null);
  const [error, setError] = useState('');

  const addHolding = () => setHoldings([...holdings, { symbol: '', weight: 0 }]);
  const removeHolding = (i: number) => setHoldings(holdings.filter((_, idx) => idx !== i));
  const updateHolding = (i: number, field: keyof Holding, value: string | number) =>
    setHoldings(holdings.map((h, idx) => (idx === i ? { ...h, [field]: value } : h)));

  // 实时配置预览
  const alloc = useMemo(
    () => computeAllocation(age, investorType, stockPickPercent / 100),
    [age, investorType, stockPickPercent],
  );

  // 资格校验提示
  const profileWarning = useMemo(() => {
    if (investorType === 'experienced') {
      if (experienceYears === '0-5' || annualReturn === '0-10') {
        return '经验丰富投资人要求：≥5年经验 且 年化收益≥10%';
      }
    }
    if (investorType === 'professional') {
      if (experienceYears !== '10+' || beatSP500 !== 'yes') {
        return '专业投资人要求：≥10年经验 且 十年长期业绩跑赢标普500';
      }
    }
    return '';
  }, [investorType, experienceYears, annualReturn, beatSP500]);

  // 个股占总配置上限（百分比）
  const equity = Math.max(0, 100 - 10 - Math.floor(age / 10) * 10);
  const maxStockPick = investorType === 'novice' ? 0 : investorType === 'experienced' ? Math.min(equity, 50) : equity;

  const handleSubmit = async () => {
    setLoading(true);
    setError('');
    setResult(null);
    try {
      const body: PlanRequest = {
        profile: {
          age,
          investor_type: investorType,
          experience_years: experienceYears,
          annualized_return: annualReturn,
          beat_sp500_10y: beatSP500,
          individual_stock_percent: stockPickPercent / 100,
        },
        portfolio: holdings.filter((h) => h.symbol.trim()),
        constraints: {
          min_single_asset: 0.025,
        },
      };
      const res = await postPlan(body);
      setResult(res);
    } catch (e) {
      setError(e instanceof Error ? e.message : '请求失败');
    } finally {
      setLoading(false);
    }
  };

  const totalWeight = holdings.reduce((s, h) => s + (Number(h.weight) || 0), 0);

  return (
    <div className="h-full overflow-y-auto">
      <div className="max-w-6xl mx-auto p-8">
        <div className="flex items-center gap-2 mb-2">
          <PieChart className="w-6 h-6 text-blue-400" />
          <h1 className="text-2xl font-bold">配置建议</h1>
        </div>
        <p className="text-xs text-gray-500 mb-6">基于「守拙价值多元化基金理念」，根据年龄与投资人类型自动计算资产配置。</p>

        <div className="grid grid-cols-1 lg:grid-cols-2 gap-8">
          {/* ── 左侧：输入表单 ── */}
          <div className="space-y-6">
            {/* 投资人画像 */}
            <section className="bg-gray-900 rounded-xl p-5 border border-gray-800">
              <h2 className="text-sm font-semibold text-gray-300 mb-4">投资人画像</h2>
              <div className="space-y-4">
                {/* 年龄 */}
                <div>
                  <label className="block text-xs text-gray-500 mb-1">年龄</label>
                  <div className="flex items-center gap-3">
                    <input
                      type="range"
                      min="18"
                      max="70"
                      value={age}
                      onChange={(e) => setAge(parseInt(e.target.value))}
                      className="flex-1 accent-blue-500"
                    />
                    <span className="w-10 text-center text-sm font-mono text-blue-300">{age}</span>
                  </div>
                  <p className="text-xs text-gray-600 mt-1">
                    债券占比 = {Math.floor(age / 10) * 10}% · 黄金 10% · 股票 {Math.max(0, 100 - Math.floor(age / 10) * 10 - 10)}%
                  </p>
                </div>

                {/* 投资人类型 */}
                <div>
                  <label className="block text-xs text-gray-500 mb-1.5">投资人类型</label>
                  <div className="grid grid-cols-3 gap-2">
                    {([
                      { value: 'novice' as const, label: '小白', desc: '不配个股' },
                      { value: 'experienced' as const, label: '经验丰富', desc: '个股≤50%' },
                      { value: 'professional' as const, label: '专业投资人', desc: '不设上限' },
                    ]).map(({ value, label, desc }) => (
                      <button
                        key={value}
                        onClick={() => {
                          setInvestorType(value);
                          if (value === 'novice') setStockPickPercent(0);
                        }}
                        className={`px-3 py-2 rounded-lg text-xs font-medium border transition-colors cursor-pointer ${
                          investorType === value
                            ? 'bg-blue-600/20 border-blue-500 text-blue-300'
                            : 'bg-gray-800 border-gray-700 text-gray-400 hover:border-gray-600'
                        }`}
                      >
                        <div>{label}</div>
                        <div className="text-[10px] opacity-60 mt-0.5">{desc}</div>
                      </button>
                    ))}
                  </div>
                </div>

                {/* 资格验证（经验丰富/专业） */}
                {investorType !== 'novice' && (
                  <div className="bg-gray-800/50 rounded-lg p-3 space-y-3 border border-gray-700/50">
                    <p className="text-xs text-gray-400 flex items-center gap-1">
                      <Info className="w-3.5 h-3.5 text-blue-400" />
                      资格自评（self-declared）
                    </p>
                    <div className="grid grid-cols-2 gap-3">
                      <div>
                        <label className="block text-[10px] text-gray-500 mb-1">投资经验</label>
                        <select
                          value={experienceYears}
                          onChange={(e) => setExperienceYears(e.target.value)}
                          className="w-full bg-gray-800 border border-gray-700 rounded-lg px-2 py-1.5 text-xs text-gray-100 focus:outline-none focus:border-blue-500"
                        >
                          <option value="0-5">0~5年</option>
                          <option value="5-10">5~10年</option>
                          <option value="10+">10年以上</option>
                        </select>
                      </div>
                      <div>
                        <label className="block text-[10px] text-gray-500 mb-1">年化收益区间</label>
                        <select
                          value={annualReturn}
                          onChange={(e) => setAnnualReturn(e.target.value)}
                          className="w-full bg-gray-800 border border-gray-700 rounded-lg px-2 py-1.5 text-xs text-gray-100 focus:outline-none focus:border-blue-500"
                        >
                          <option value="0-10">0~10%</option>
                          <option value="10-20">10~20%</option>
                          <option value="20+">20%以上</option>
                        </select>
                      </div>
                    </div>
                    {investorType === 'professional' && (
                      <div>
                        <label className="block text-[10px] text-gray-500 mb-1">十年期业绩是否跑赢标普500？</label>
                        <select
                          value={beatSP500}
                          onChange={(e) => setBeatSP500(e.target.value)}
                          className="w-full bg-gray-800 border border-gray-700 rounded-lg px-2 py-1.5 text-xs text-gray-100 focus:outline-none focus:border-blue-500"
                        >
                          <option value="no">否</option>
                          <option value="yes">是</option>
                        </select>
                      </div>
                    )}
                    {profileWarning && (
                      <p className="text-[11px] text-amber-400 flex items-start gap-1">
                        <AlertTriangle className="w-3 h-3 mt-0.5 flex-shrink-0" />
                        {profileWarning}
                      </p>
                    )}
                  </div>
                )}

                {/* 个股占比（占总配置） */}
                {investorType !== 'novice' && (
                  <div>
                    <label className="block text-xs text-gray-500 mb-1">
                      个股占总配置的比例: {stockPickPercent}%
                      <span className="text-gray-600">（上限 {maxStockPick}%）</span>
                    </label>
                    <input
                      type="range"
                      min="0"
                      max={maxStockPick}
                      step="1"
                      value={Math.min(stockPickPercent, maxStockPick)}
                      onChange={(e) => setStockPickPercent(parseInt(e.target.value))}
                      className="w-full accent-blue-500"
                    />
                  </div>
                )}
              </div>
            </section>

            {/* 实时配置预览 */}
            <section className="bg-gray-900 rounded-xl p-5 border border-gray-800">
              <h2 className="text-sm font-semibold text-gray-300 mb-3">建议配置预览</h2>
              <div className="space-y-2">
                {[
                  { label: '黄金 (GLD)', value: alloc.gold, color: 'bg-yellow-500' },
                  { label: '债券 (VGIT)', value: alloc.bond, color: 'bg-emerald-500' },
                  { label: '美股 (VOO)', value: alloc.us, color: 'bg-blue-500' },
                  { label: 'A股 (沪深300)', value: alloc.a, color: 'bg-red-500' },
                  { label: '港股 (恒生指数)', value: alloc.hk, color: 'bg-purple-500' },
                  ...(alloc.stockPick > 0
                    ? [{ label: '个股仓位', value: alloc.stockPick, color: 'bg-orange-500' }]
                    : []),
                ].map(({ label, value, color }) => (
                  <div key={label} className="flex items-center gap-3">
                    <span className="w-28 text-xs text-gray-400 truncate">{label}</span>
                    <div className="flex-1 bg-gray-800 rounded-full h-2">
                      <div
                        className={`${color} h-2 rounded-full transition-all`}
                        style={{ width: `${value * 100}%` }}
                      />
                    </div>
                    <span className="w-12 text-right text-xs text-gray-400">
                      {(value * 100).toFixed(1)}%
                    </span>
                  </div>
                ))}
              </div>
              <p className="text-[10px] text-gray-600 mt-2">
                指数比例：港股:A股:美股 = 1:3:16 · 单一资产下限 2.5%
              </p>
            </section>

            {/* 当前持仓 */}
            <section className="bg-gray-900 rounded-xl p-5 border border-gray-800">
              <div className="flex items-center justify-between mb-4">
                <h2 className="text-sm font-semibold text-gray-300">当前持仓（可选）</h2>
                <span className={`text-xs ${Math.abs(totalWeight - 1) < 0.01 ? 'text-green-400' : 'text-amber-400'}`}>
                  总权重: {(totalWeight * 100).toFixed(0)}%
                </span>
              </div>
              <div className="space-y-2">
                {holdings.map((h, i) => (
                  <div key={i} className="flex items-center gap-2">
                    <input
                      type="text"
                      value={h.symbol}
                      onChange={(e) => updateHolding(i, 'symbol', e.target.value.toUpperCase())}
                      placeholder="代码"
                      className="flex-1 bg-gray-800 border border-gray-700 rounded-lg px-3 py-2 text-sm text-gray-100 focus:outline-none focus:border-blue-500"
                    />
                    <input
                      type="number"
                      value={h.weight}
                      onChange={(e) => updateHolding(i, 'weight', parseFloat(e.target.value) || 0)}
                      step="0.05"
                      min="0"
                      max="1"
                      className="w-24 bg-gray-800 border border-gray-700 rounded-lg px-3 py-2 text-sm text-gray-100 focus:outline-none focus:border-blue-500"
                    />
                    <button onClick={() => removeHolding(i)} className="text-gray-600 hover:text-red-400 cursor-pointer">
                      <Trash2 className="w-4 h-4" />
                    </button>
                  </div>
                ))}
              </div>
              <button
                onClick={addHolding}
                className="mt-3 flex items-center gap-1 text-xs text-blue-400 hover:text-blue-300 cursor-pointer"
              >
                <Plus className="w-3.5 h-3.5" /> 添加持仓
              </button>
            </section>

            {/* 提交 */}
            <button
              onClick={handleSubmit}
              disabled={loading}
              className="w-full flex items-center justify-center gap-2 px-6 py-3 bg-blue-600 hover:bg-blue-500 disabled:bg-blue-600/50 text-white font-medium rounded-xl transition-colors cursor-pointer"
            >
              {loading ? <Loader2 className="w-4 h-4 animate-spin" /> : <PieChart className="w-4 h-4" />}
              {loading ? '分析中...' : '获取配置建议'}
            </button>
          </div>

          {/* ── 右侧：结果展示 ── */}
          <div className="space-y-6">
            {error && (
              <div className="bg-red-900/30 border border-red-800 rounded-xl p-4 flex items-start gap-2 text-sm text-red-300">
                <AlertCircle className="w-4 h-4 mt-0.5 flex-shrink-0" />
                {error}
              </div>
            )}

            {result && (
              <>
                {/* 建议配置 */}
                <section className="bg-gray-900 rounded-xl p-5 border border-gray-800">
                  <h2 className="text-sm font-semibold text-gray-300 mb-4">后端返回配置</h2>
                  <div className="space-y-2">
                    {Object.entries(result.proposed_portfolio).map(([symbol, weight]) => (
                      <div key={symbol} className="flex items-center gap-3">
                        <span className="w-14 text-xs font-mono text-gray-400">{symbol}</span>
                        <div className="flex-1 bg-gray-800 rounded-full h-2.5">
                          <div
                            className="bg-blue-500 h-2.5 rounded-full transition-all"
                            style={{ width: `${(weight as number) * 100}%` }}
                          />
                        </div>
                        <span className="w-12 text-right text-xs text-gray-400">
                          {((weight as number) * 100).toFixed(0)}%
                        </span>
                      </div>
                    ))}
                  </div>
                </section>

                {/* 风控报告 */}
                <section className="bg-gray-900 rounded-xl p-5 border border-gray-800">
                  <h2 className="text-sm font-semibold text-gray-300 mb-4">风控报告</h2>
                  <div className="flex items-center gap-2 mb-3">
                    {result.risk_report.status === 'PASS' ? (
                      <span className="flex items-center gap-1 px-2.5 py-1 bg-green-900/30 border border-green-800 rounded-full text-xs text-green-300">
                        <CheckCircle2 className="w-3.5 h-3.5" /> PASS
                      </span>
                    ) : result.risk_report.status === 'WARN' ? (
                      <span className="flex items-center gap-1 px-2.5 py-1 bg-amber-900/30 border border-amber-800 rounded-full text-xs text-amber-300">
                        <AlertTriangle className="w-3.5 h-3.5" /> WARN
                      </span>
                    ) : (
                      <span className="flex items-center gap-1 px-2.5 py-1 bg-red-900/30 border border-red-800 rounded-full text-xs text-red-300">
                        <AlertCircle className="w-3.5 h-3.5" /> FAIL
                      </span>
                    )}
                  </div>
                  {result.risk_report.triggered_rules.length > 0 && (
                    <ul className="text-xs text-gray-400 space-y-1 list-disc list-inside">
                      {result.risk_report.triggered_rules.map((r, i) => (
                        <li key={i}>{r}</li>
                      ))}
                    </ul>
                  )}
                </section>

                {/* 理由 */}
                <section className="bg-gray-900 rounded-xl p-5 border border-gray-800">
                  <h2 className="text-sm font-semibold text-gray-300 mb-3">分析理由</h2>
                  <p className="text-sm text-gray-400 leading-relaxed">{result.rationale}</p>
                </section>

                {/* 操作清单 */}
                {result.rebalancing_actions.length > 0 && (
                  <section className="bg-gray-900 rounded-xl p-5 border border-gray-800">
                    <h2 className="text-sm font-semibold text-gray-300 mb-3">调仓操作</h2>
                    <ul className="text-sm text-gray-400 space-y-1.5">
                      {result.rebalancing_actions.map((a, i) => (
                        <li key={i} className="flex items-start gap-2">
                          <span className="text-blue-400 font-mono text-xs mt-0.5">{i + 1}.</span>
                          {a}
                        </li>
                      ))}
                    </ul>
                  </section>
                )}
              </>
            )}

            {!result && !error && (
              <div className="flex items-center justify-center h-64 text-gray-600 text-sm">
                填写左侧画像，点击"获取配置建议"查看结果
              </div>
            )}
          </div>
        </div>
      </div>
    </div>
  );
}
