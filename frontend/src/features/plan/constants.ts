import type { PlanResultsMap, Holding, RuleOption } from './types';

export const RULE_OPTIONS: RuleOption[] = [
  { value: 'shouzhe', label: '守拙理念', desc: '年龄驱动 + 个股分层' },
  { value: 'bridgewater', label: '桥水全天候', desc: '固定配置，风险平衡' },
  { value: 'permanent', label: '永久组合', desc: '四资产等权，懒人配置' },
  { value: 'swensen', label: '斯文森耶鲁', desc: '六资产多元化配置' },
];

export const DEFAULT_HOLDINGS: Holding[] = [
  { symbol: 'VOO', weight: 0.48 },
  { symbol: 'HS300', weight: 0.09 },
  { symbol: 'HSI', weight: 0.03 },
  { symbol: 'BND', weight: 0.30 },
  { symbol: 'GLD', weight: 0.10 },
];

export const EMPTY_PLAN_RESULTS: PlanResultsMap = {
  shouzhe: null,
  bridgewater: null,
  permanent: null,
  swensen: null,
};
