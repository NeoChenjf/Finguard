import { BrowserRouter, Routes, Route, Navigate } from 'react-router-dom';
import { useAppStore } from './store/useAppStore';
import Layout from './components/Layout';
import StartupPage from './pages/StartupPage';
import SettingsPage from './pages/SettingsPage';
import PlanPage from './pages/PlanPage';
import ValueCellPage from './pages/ValueCellPage';

export default function App() {
  const isConnected = useAppStore((s) => s.isBackendConnected);
  const hasApiKey = useAppStore((s) => s.hasApiKey);

  // 未连接后端时，只显示启动页
  if (!isConnected) {
    return (
      <BrowserRouter>
        <StartupPage />
      </BrowserRouter>
    );
  }

  return (
    <BrowserRouter>
      <Routes>
        <Route element={<Layout />}>
          <Route path="/settings" element={<SettingsPage />} />
          <Route path="/plan" element={<PlanPage />} />
          <Route path="/valuecell" element={<ValueCellPage />} />
          {/* 首次启动未配置 API Key → 引导至设置页 */}
          <Route path="*" element={<Navigate to={hasApiKey ? '/valuecell' : '/settings'} replace />} />
        </Route>
      </Routes>
    </BrowserRouter>
  );
}
