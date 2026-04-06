import { useMemo, useState, useEffect } from 'react';

import { postPlan, type PlanRequest, type PlanResponse } from '../../api/client';
import { useAppStore } from '../../store/useAppStore';

import { EMPTY_PLAN_RESULTS } from './constants';
import {
  computeAllocation,
  getActiveResults,
  getCompareSymbols,
  getMaxStockPick,
  getProfileWarning,
} from './utils';
import type { Holding, InvestorType, PlanResultsMap, RuleName } from './types';

function createEmptyResults(): PlanResultsMap {
  return { ...EMPTY_PLAN_RESULTS };
}

export function usePlanPage() {
  const planState = useAppStore((s) => s.planState);
  const setPlanState = useAppStore((s) => s.setPlanState);

  const [age, setAge] = useState(planState.age);
  const [investorType, setInvestorType] = useState<InvestorType>(planState.investorType);
  const [experienceYears, setExperienceYears] = useState<string>(planState.experienceYears);
  const [annualReturn, setAnnualReturn] = useState<string>(planState.annualReturn);
  const [beatSP500, setBeatSP500] = useState<string>(planState.beatSP500);
  const [stockPickPercent, setStockPickPercent] = useState(planState.stockPickPercent);
  const [holdings, setHoldings] = useState<Holding[]>(planState.holdings);
  const [selectedRules, setSelectedRules] = useState<RuleName[]>(planState.selectedRules);
  const [loading, setLoading] = useState(false);
  const [results, setResults] = useState<PlanResultsMap>(planState.results);
  const [error, setError] = useState('');

  const alloc = useMemo(
    () => computeAllocation(age, investorType, stockPickPercent / 100),
    [age, investorType, stockPickPercent],
  );

  const profileWarning = useMemo(
    () => getProfileWarning(investorType, experienceYears, annualReturn, beatSP500),
    [investorType, experienceYears, annualReturn, beatSP500],
  );

  const maxStockPick = useMemo(
    () => getMaxStockPick(age, investorType),
    [age, investorType],
  );

  const totalWeight = useMemo(
    () => holdings.reduce((sum, holding) => sum + (Number(holding.weight) || 0), 0),
    [holdings],
  );

  const activeResults = useMemo(
    () => getActiveResults(selectedRules, results),
    [selectedRules, results],
  );

  const hasResults = activeResults.length > 0;

  const compareSymbols = useMemo(
    () => getCompareSymbols(activeResults),
    [activeResults],
  );

  const addHolding = () => setHoldings([...holdings, { symbol: '', weight: 0 }]);

  const removeHolding = (index: number) => {
    setHoldings(holdings.filter((_, itemIndex) => itemIndex !== index));
  };

  const updateHolding = (index: number, field: keyof Holding, value: string | number) => {
    setHoldings(
      holdings.map((holding, itemIndex) =>
        itemIndex === index ? { ...holding, [field]: value } : holding,
      ),
    );
  };

  const updateInvestorType = (value: InvestorType) => {
    setInvestorType(value);
    if (value === 'novice') {
      setStockPickPercent(0);
    }
  };

  const toggleRule = (rule: RuleName) => {
    setSelectedRules((prev) => {
      if (prev.includes(rule)) {
        if (prev.length === 1) return prev;
        return prev.filter((item) => item !== rule);
      }
      return [...prev, rule];
    });
  };

  const selectAllRules = () => setSelectedRules(['shouzhe', 'bridgewater', 'permanent', 'swensen']);
  const keepOnlyShouzhe = () => setSelectedRules(['shouzhe']);

  const handleSubmit = async () => {
    setLoading(true);
    setError('');
    setResults(createEmptyResults());

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
        portfolio: holdings.filter((holding) => holding.symbol.trim()),
        constraints: {
          min_single_asset: 0.025,
        },
      };

      const ruleResults = await Promise.all(
        selectedRules.map(async (rule) => ({
          rule,
          data: await postPlan(body, rule),
        })),
      );

      const merged = createEmptyResults();
      for (const item of ruleResults) {
        merged[item.rule] = item.data as PlanResponse;
      }
      setResults(merged);

      // 保存到全局状态
      setPlanState({
        age,
        investorType,
        experienceYears,
        annualReturn,
        beatSP500,
        stockPickPercent,
        holdings,
        selectedRules,
        results: merged,
        lastSubmittedAt: new Date().toISOString(),
      });
    } catch (e) {
      setError(e instanceof Error ? e.message : '请求失败');
    } finally {
      setLoading(false);
    }
  };

  // 持久化状态到 localStorage (debounced)
  useEffect(() => {
    const timer = setTimeout(() => {
      import('../../api/store').then(({ savePageState }) => {
        savePageState('plan_state', {
          age,
          investorType,
          experienceYears,
          annualReturn,
          beatSP500,
          stockPickPercent,
          holdings,
          selectedRules,
          results,
          lastSubmittedAt: planState.lastSubmittedAt,
        });
      });
    }, 500);
    return () => clearTimeout(timer);
  }, [age, investorType, experienceYears, annualReturn, beatSP500, stockPickPercent, holdings, selectedRules, results, planState.lastSubmittedAt]);

  // 从 localStorage 恢复状态
  useEffect(() => {
    import('../../api/store').then(({ loadPageState }) => {
      loadPageState<typeof planState>('plan_state').then((saved) => {
        if (saved) {
          setPlanState(saved);
          setAge(saved.age);
          setInvestorType(saved.investorType);
          setExperienceYears(saved.experienceYears);
          setAnnualReturn(saved.annualReturn);
          setBeatSP500(saved.beatSP500);
          setStockPickPercent(saved.stockPickPercent);
          setHoldings(saved.holdings);
          setSelectedRules(saved.selectedRules);
          setResults(saved.results);
        }
      });
    });
  }, []);

  return {
    age,
    investorType,
    experienceYears,
    annualReturn,
    beatSP500,
    stockPickPercent,
    holdings,
    selectedRules,
    loading,
    results,
    error,
    alloc,
    profileWarning,
    maxStockPick,
    totalWeight,
    activeResults,
    hasResults,
    compareSymbols,
    setAge,
    updateInvestorType,
    setExperienceYears,
    setAnnualReturn,
    setBeatSP500,
    setStockPickPercent,
    addHolding,
    removeHolding,
    updateHolding,
    toggleRule,
    selectAllRules,
    keepOnlyShouzhe,
    handleSubmit,
  };
}
