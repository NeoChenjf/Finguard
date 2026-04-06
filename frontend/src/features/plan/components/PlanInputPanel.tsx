import {
  AlertTriangle,
  Info,
  Loader2,
  PieChart,
  Plus,
  Trash2,
} from 'lucide-react';

import { RULE_OPTIONS } from '../constants';
import type { Holding, InvestorType } from '../types';
import type { usePlanPage } from '../usePlanPage';

type PlanPageState = ReturnType<typeof usePlanPage>;

interface PlanInputPanelProps {
  state: PlanPageState;
}

interface PreviewRow {
  label: string;
  value: number;
  color: string;
}

const INVESTOR_OPTIONS: Array<{ value: InvestorType; label: string; desc: string }> = [
  { value: 'novice', label: '小白', desc: '不配个股' },
  { value: 'experienced', label: '经验丰富', desc: '个股≤50%' },
  { value: 'professional', label: '专业投资人', desc: '不设上限' },
];

const PREVIEW_META = [
  { label: '黄金 (GLD)', key: 'gold', color: 'bg-yellow-500' },
  { label: '债券 (BND)', key: 'bond', color: 'bg-emerald-500' },
  { label: '美股 (VOO)', key: 'us', color: 'bg-blue-500' },
  { label: 'A股 (HS300)', key: 'a', color: 'bg-red-500' },
  { label: '港股 (HSI)', key: 'hk', color: 'bg-purple-500' },
] as const;

function renderHoldingRow(
  holding: Holding,
  index: number,
  state: PlanPageState,
) {
  return (
    <div key={index} className="flex items-center gap-2">
      <input
        type="text"
        value={holding.symbol}
        onChange={(e) => state.updateHolding(index, 'symbol', e.target.value.toUpperCase())}
        placeholder="代码"
        className="flex-1 bg-gray-800 border border-gray-700 rounded-lg px-3 py-2 text-sm text-gray-100 focus:outline-none focus:border-blue-500"
      />
      <input
        type="number"
        value={holding.weight}
        onChange={(e) => state.updateHolding(index, 'weight', parseFloat(e.target.value) || 0)}
        step="0.05"
        min="0"
        max="1"
        className="w-24 bg-gray-800 border border-gray-700 rounded-lg px-3 py-2 text-sm text-gray-100 focus:outline-none focus:border-blue-500"
      />
      <button
        onClick={() => state.removeHolding(index)}
        className="text-gray-600 hover:text-red-400 cursor-pointer"
      >
        <Trash2 className="w-4 h-4" />
      </button>
    </div>
  );
}

export function PlanInputPanel({ state }: PlanInputPanelProps) {
  const previewRows: PreviewRow[] = PREVIEW_META.map(({ label, key, color }) => ({
    label,
    color,
    value: state.alloc[key],
  }));

  if (state.alloc.stockPick > 0) {
    previewRows.push({
      label: '个股仓位',
      value: state.alloc.stockPick,
      color: 'bg-orange-500',
    });
  }

  return (
    <div className="space-y-6">
      <section className="bg-gray-900 rounded-xl p-5 border border-gray-800">
        <div className="flex items-center justify-between mb-3">
          <h2 className="text-sm font-semibold text-gray-300">资产配置规则（可多选）</h2>
          <div className="flex items-center gap-2">
            <button
              onClick={state.selectAllRules}
              className="text-[11px] text-blue-400 hover:text-blue-300 cursor-pointer"
            >
              全选
            </button>
            <button
              onClick={state.keepOnlyShouzhe}
              className="text-[11px] text-gray-400 hover:text-gray-300 cursor-pointer"
            >
              仅守拙
            </button>
          </div>
        </div>
        <div className="grid grid-cols-2 gap-2">
          {RULE_OPTIONS.map((item) => {
            const active = state.selectedRules.includes(item.value);
            return (
              <button
                key={item.value}
                onClick={() => state.toggleRule(item.value)}
                className={`px-3 py-2 rounded-lg text-xs text-left border transition-colors cursor-pointer ${
                  active
                    ? 'bg-blue-600/20 border-blue-500 text-blue-300'
                    : 'bg-gray-800 border-gray-700 text-gray-400 hover:border-gray-600'
                }`}
              >
                <div className="font-medium">{item.label}</div>
                <div className="text-[10px] opacity-70 mt-0.5">{item.desc}</div>
              </button>
            );
          })}
        </div>
      </section>

      <section className="bg-gray-900 rounded-xl p-5 border border-gray-800">
        <h2 className="text-sm font-semibold text-gray-300 mb-4">投资人画像</h2>
        <div className="space-y-4">
          <div>
            <label className="block text-xs text-gray-500 mb-1">年龄</label>
            <div className="flex items-center gap-3">
              <input
                type="range"
                min="18"
                max="70"
                value={state.age}
                onChange={(e) => state.setAge(parseInt(e.target.value))}
                className="flex-1 accent-blue-500"
              />
              <span className="w-10 text-center text-sm font-mono text-blue-300">{state.age}</span>
            </div>
            <p className="text-xs text-gray-600 mt-1">
              债券占比 = {Math.floor(state.age / 10) * 10}% · 黄金 10% · 股票{' '}
              {Math.max(0, 100 - Math.floor(state.age / 10) * 10 - 10)}%
            </p>
          </div>

          <div>
            <label className="block text-xs text-gray-500 mb-1.5">投资人类型</label>
            <div className="grid grid-cols-3 gap-2">
              {INVESTOR_OPTIONS.map(({ value, label, desc }) => (
                <button
                  key={value}
                  onClick={() => state.updateInvestorType(value)}
                  className={`px-3 py-2 rounded-lg text-xs font-medium border transition-colors cursor-pointer ${
                    state.investorType === value
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

          {state.investorType !== 'novice' && (
            <div className="bg-gray-800/50 rounded-lg p-3 space-y-3 border border-gray-700/50">
              <p className="text-xs text-gray-400 flex items-center gap-1">
                <Info className="w-3.5 h-3.5 text-blue-400" />
                资格自评（self-declared）
              </p>
              <div className="grid grid-cols-2 gap-3">
                <div>
                  <label className="block text-[10px] text-gray-500 mb-1">投资经验</label>
                  <select
                    value={state.experienceYears}
                    onChange={(e) => state.setExperienceYears(e.target.value)}
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
                    value={state.annualReturn}
                    onChange={(e) => state.setAnnualReturn(e.target.value)}
                    className="w-full bg-gray-800 border border-gray-700 rounded-lg px-2 py-1.5 text-xs text-gray-100 focus:outline-none focus:border-blue-500"
                  >
                    <option value="0-10">0~10%</option>
                    <option value="10-20">10~20%</option>
                    <option value="20+">20%以上</option>
                  </select>
                </div>
              </div>
              {state.investorType === 'professional' && (
                <div>
                  <label className="block text-[10px] text-gray-500 mb-1">
                    十年期业绩是否跑赢标普500？
                  </label>
                  <select
                    value={state.beatSP500}
                    onChange={(e) => state.setBeatSP500(e.target.value)}
                    className="w-full bg-gray-800 border border-gray-700 rounded-lg px-2 py-1.5 text-xs text-gray-100 focus:outline-none focus:border-blue-500"
                  >
                    <option value="no">否</option>
                    <option value="yes">是</option>
                  </select>
                </div>
              )}
              {state.profileWarning && (
                <p className="text-[11px] text-amber-400 flex items-start gap-1">
                  <AlertTriangle className="w-3 h-3 mt-0.5 flex-shrink-0" />
                  {state.profileWarning}
                </p>
              )}
            </div>
          )}

          {state.investorType !== 'novice' && (
            <div>
              <label className="block text-xs text-gray-500 mb-1">
                个股占总配置的比例: {state.stockPickPercent}%
                <span className="text-gray-600">（上限 {state.maxStockPick}%）</span>
              </label>
              <input
                type="range"
                min="0"
                max={state.maxStockPick}
                step="1"
                value={Math.min(state.stockPickPercent, state.maxStockPick)}
                onChange={(e) => state.setStockPickPercent(parseInt(e.target.value))}
                className="w-full accent-blue-500"
              />
            </div>
          )}
        </div>
      </section>

      <section className="bg-gray-900 rounded-xl p-5 border border-gray-800">
        <h2 className="text-sm font-semibold text-gray-300 mb-3">建议配置预览</h2>
        <div className="space-y-2">
          {previewRows.map(({ label, value, color }) => (
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
          当前为守拙规则预览；最终以后端各规则返回为准。
        </p>
      </section>

      <section className="bg-gray-900 rounded-xl p-5 border border-gray-800">
        <div className="flex items-center justify-between mb-4">
          <h2 className="text-sm font-semibold text-gray-300">当前持仓（可选）</h2>
          <span
            className={`text-xs ${
              Math.abs(state.totalWeight - 1) < 0.01 ? 'text-green-400' : 'text-amber-400'
            }`}
          >
            总权重: {(state.totalWeight * 100).toFixed(0)}%
          </span>
        </div>
        <div className="space-y-2">
          {state.holdings.map((holding, index) => renderHoldingRow(holding, index, state))}
        </div>
        <button
          onClick={state.addHolding}
          className="mt-3 flex items-center gap-1 text-xs text-blue-400 hover:text-blue-300 cursor-pointer"
        >
          <Plus className="w-3.5 h-3.5" /> 添加持仓
        </button>
      </section>

      <button
        onClick={state.handleSubmit}
        disabled={state.loading}
        className="w-full flex items-center justify-center gap-2 px-6 py-3 bg-blue-600 hover:bg-blue-500 disabled:bg-blue-600/50 text-white font-medium rounded-xl transition-colors cursor-pointer"
      >
        {state.loading ? (
          <Loader2 className="w-4 h-4 animate-spin" />
        ) : (
          <PieChart className="w-4 h-4" />
        )}
        {state.loading ? '分析中...' : `生成对比方案（${state.selectedRules.length}个规则）`}
      </button>
    </div>
  );
}
