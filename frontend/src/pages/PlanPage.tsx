import { PieChart } from 'lucide-react';

import { PlanInputPanel } from '../features/plan/components/PlanInputPanel';
import { PlanResultsPanel } from '../features/plan/components/PlanResultsPanel';
import { usePlanPage } from '../features/plan/usePlanPage';

export default function PlanPage() {
  const state = usePlanPage();

  return (
    <div className="h-full overflow-y-auto">
      <div className="max-w-6xl mx-auto p-8">
        <div className="flex items-center gap-2 mb-2">
          <PieChart className="w-6 h-6 text-blue-400" />
          <h1 className="text-2xl font-bold">配置建议</h1>
        </div>
        <p className="text-xs text-gray-500 mb-6">
          支持多规则对比：守拙理念 / 桥水全天候 / 永久组合 / 斯文森耶鲁。
        </p>

        <div className="grid grid-cols-1 lg:grid-cols-2 gap-8">
          <PlanInputPanel state={state} />
          <PlanResultsPanel state={state} />
        </div>
      </div>
    </div>
  );
}
