import type { AllocationPreview, ActiveRuleResult, InvestorType, PlanResultsMap, RuleName } from './types';

export function computeAllocation(
  age: number,
  investorType: InvestorType,
  stockPickPctTotal: number,
): AllocationPreview {
  const gold = 0.10;
  const bondDecade = Math.floor(age / 10) * 10;
  const bond = bondDecade / 100;
  const equity = Math.max(0, 1 - gold - bond);

  const individualAllowed =
    investorType === 'experienced' || investorType === 'professional';
  const stockPick = individualAllowed ? Math.min(stockPickPctTotal, equity) : 0;
  const indexEquity = equity - stockPick;

  const totalRatio = 1 + 3 + 16;
  const hk = indexEquity * (1 / totalRatio);
  const a = indexEquity * (3 / totalRatio);
  const us = indexEquity * (16 / totalRatio);

  return { gold, bond, equity, hk, a, us, stockPick, individualAllowed };
}

export function getProfileWarning(
  investorType: InvestorType,
  experienceYears: string,
  annualReturn: string,
  beatSP500: string,
): string {
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
}

export function getMaxStockPick(age: number, investorType: InvestorType): number {
  const equity = Math.max(0, 100 - 10 - Math.floor(age / 10) * 10);
  if (investorType === 'novice') return 0;
  if (investorType === 'experienced') return Math.min(equity, 50);
  return equity;
}

export function getActiveResults(
  selectedRules: RuleName[],
  results: PlanResultsMap,
): ActiveRuleResult[] {
  return selectedRules
    .map((rule) => ({ rule, data: results[rule] }))
    .filter((item): item is ActiveRuleResult => item.data !== null);
}

export function getCompareSymbols(activeResults: ActiveRuleResult[]): string[] {
  return Array.from(
    new Set(activeResults.flatMap((item) => Object.keys(item.data.proposed_portfolio))),
  );
}
