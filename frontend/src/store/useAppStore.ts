import { create } from 'zustand';
import type { ValueCellResponse } from '../api/client';
import type { Holding, InvestorType, PlanResultsMap, RuleName } from '../features/plan/types';

interface AppState {
  /** 后端是否可达 */
  isBackendConnected: boolean;
  /** API Key 是否已配置 */
  hasApiKey: boolean;
  /** 用户在设置页输入的 API Key（内存中保留，用于 chat 请求鉴权） */
  apiKey: string;
  /** 后端配置快照 */
  settings: {
    api_base: string;
    model: string;
    temperature: number;
    timeout_ms: number;
    api_key_hint: string;
    valuecell_db_profile: string;
    valuecell_db_profile_active?: string;
    valuecell_db_profile_label?: string;
    valuecell_db_path_hint: string;
    valuecell_db_profiles: Array<{
      key: string;
      label: string;
      description: string;
      path_hint: string;
      exists: boolean;
      is_demo: boolean;
    }>;
  } | null;

  /** ValueCell 页面状态 */
  valueCellState: {
    symbol: string;
    result: ValueCellResponse | null;
    lastAnalyzedAt: string | null;
  };

  /** Plan 页面状态 */
  planState: {
    age: number;
    investorType: InvestorType;
    experienceYears: string;
    annualReturn: string;
    beatSP500: string;
    stockPickPercent: number;
    holdings: Holding[];
    selectedRules: RuleName[];
    results: PlanResultsMap;
    lastSubmittedAt: string | null;
  };

  setBackendConnected: (v: boolean) => void;
  setHasApiKey: (v: boolean) => void;
  setApiKey: (v: string) => void;
  setSettings: (s: AppState['settings']) => void;
  setValueCellState: (state: Partial<AppState['valueCellState']>) => void;
  setPlanState: (state: Partial<AppState['planState']>) => void;
}

export const useAppStore = create<AppState>((set) => ({
  isBackendConnected: false,
  hasApiKey: false,
  apiKey: '',
  settings: null,

  valueCellState: {
    symbol: '',
    result: null,
    lastAnalyzedAt: null,
  },

  planState: {
    age: 30,
    investorType: 'novice',
    experienceYears: '0-5',
    annualReturn: '0-10',
    beatSP500: 'no',
    stockPickPercent: 0,
    holdings: [
      { symbol: 'VOO', weight: 0.48 },
      { symbol: 'HS300', weight: 0.09 },
      { symbol: 'HSI', weight: 0.03 },
      { symbol: 'BND', weight: 0.30 },
      { symbol: 'GLD', weight: 0.10 },
    ],
    selectedRules: ['shouzhe'],
    results: {
      shouzhe: null,
      bridgewater: null,
      permanent: null,
      swensen: null,
    },
    lastSubmittedAt: null,
  },

  setBackendConnected: (v) => set({ isBackendConnected: v }),
  setHasApiKey: (v) => set({ hasApiKey: v }),
  setApiKey: (v) => set({ apiKey: v }),
  setSettings: (s) => set({ settings: s }),
  setValueCellState: (state) => set((prev) => ({
    valueCellState: { ...prev.valueCellState, ...state },
  })),
  setPlanState: (state) => set((prev) => ({
    planState: { ...prev.planState, ...state },
  })),
}));
