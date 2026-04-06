import type { PlanResponse } from '../../api/client';

export interface Holding {
  symbol: string;
  weight: number;
}

export type InvestorType = 'novice' | 'experienced' | 'professional';
export type RuleName = 'shouzhe' | 'bridgewater' | 'permanent' | 'swensen';

export interface RuleOption {
  value: RuleName;
  label: string;
  desc: string;
}

export interface AllocationPreview {
  gold: number;
  bond: number;
  equity: number;
  hk: number;
  a: number;
  us: number;
  stockPick: number;
  individualAllowed: boolean;
}

export type PlanResultsMap = Record<RuleName, PlanResponse | null>;

export interface ActiveRuleResult {
  rule: RuleName;
  data: PlanResponse;
}
