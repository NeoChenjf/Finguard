import { NavLink, Outlet, useNavigate } from 'react-router-dom';
import { MessageSquare, Settings, PieChart, Activity, Shield, TrendingUp } from 'lucide-react';
import { useAppStore } from '../store/useAppStore';

const navItems = [
  { to: '/chat', icon: MessageSquare, label: '投研问答' },
  { to: '/plan', icon: PieChart, label: '配置建议' },
  { to: '/valuecell', icon: TrendingUp, label: '价值分析' },
  { to: '/settings', icon: Settings, label: '设置' },
];

export default function Layout() {
  const hasApiKey = useAppStore((s) => s.hasApiKey);
  const navigate = useNavigate();

  return (
    <div className="flex h-screen bg-gray-950 text-gray-100">
      {/* 侧边栏 */}
      <aside className="w-56 flex-shrink-0 bg-gray-900 border-r border-gray-800 flex flex-col">
        {/* Logo */}
        <div className="px-4 py-5 flex items-center gap-2 border-b border-gray-800">
          <Shield className="w-6 h-6 text-blue-400" />
          <span className="text-lg font-bold tracking-tight">FinGuard</span>
        </div>

        {/* 导航 */}
        <nav className="flex-1 px-2 py-4 space-y-1">
          {navItems.map(({ to, icon: Icon, label }) => (
            <NavLink
              key={to}
              to={to}
              className={({ isActive }) =>
                `flex items-center gap-3 px-3 py-2.5 rounded-lg text-sm font-medium transition-colors ${
                  isActive
                    ? 'bg-blue-600/20 text-blue-300'
                    : 'text-gray-400 hover:bg-gray-800 hover:text-gray-200'
                }`
              }
            >
              <Icon className="w-4.5 h-4.5" />
              {label}
            </NavLink>
          ))}
        </nav>

        {/* 底部状态栏 */}
        <div className="px-4 py-3 border-t border-gray-800 text-xs text-gray-500">
          <div className="flex items-center gap-1.5">
            <Activity className="w-3 h-3 text-green-400" />
            <span>服务运行中</span>
          </div>
          {!hasApiKey && (
            <button
              onClick={() => navigate('/settings')}
              className="mt-1.5 text-amber-400 hover:text-amber-300 underline cursor-pointer"
            >
              ⚠ 请先配置 API Key
            </button>
          )}
        </div>
      </aside>

      {/* 主内容区 */}
      <main className="flex-1 overflow-hidden">
        <Outlet />
      </main>
    </div>
  );
}
