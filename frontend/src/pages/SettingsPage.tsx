import { useEffect, useState } from 'react';
import { Save, CheckCircle2, AlertCircle, Eye, EyeOff } from 'lucide-react';
import { getSettings, postSettings } from '../api/client';
import { useAppStore } from '../store/useAppStore';
import { savePersistedSettings } from '../api/store';

export default function SettingsPage() {
  const setHasApiKey = useAppStore((s) => s.setHasApiKey);
  const setApiKey = useAppStore((s) => s.setApiKey);
  const setSettings = useAppStore((s) => s.setSettings);

  const [apiKeyInput, setApiKeyInput] = useState('');
  const [apiBase, setApiBase] = useState('');
  const [model, setModel] = useState('');
  const [temperature, setTemperature] = useState(0.7);
  const [timeoutMs, setTimeoutMs] = useState(30000);
  const [apiKeyHint, setApiKeyHint] = useState('');
  const [showKey, setShowKey] = useState(false);

  const [saving, setSaving] = useState(false);
  const [toast, setToast] = useState<{ type: 'success' | 'error'; msg: string } | null>(null);

  // 加载当前设置
  useEffect(() => {
    getSettings()
      .then((s) => {
        setApiBase(s.api_base);
        setModel(s.model);
        setTemperature(s.temperature);
        setTimeoutMs(s.timeout_ms);
        setApiKeyHint(s.api_key_hint);
      })
      .catch(() => {});
  }, []);

  const handleSave = async () => {
    setSaving(true);
    setToast(null);
    try {
      const body: Record<string, unknown> = {
        api_base: apiBase,
        model,
        temperature,
        timeout_ms: timeoutMs,
      };
      // 只在用户明确输入了新的 API Key 时才发送
      if (apiKeyInput.trim()) {
        body.api_key = apiKeyInput.trim();
      }
      await postSettings(body);

      // 同步持久化到本地存储（Tauri: plugin-store；Dev: localStorage）
      await savePersistedSettings({
        api_base: apiBase,
        model,
        temperature,
        timeout_ms: timeoutMs,
        ...(apiKeyInput.trim() ? { api_key: apiKeyInput.trim() } : {}),
      });

      // 保存成功后更新全局状态
      if (apiKeyInput.trim()) {
        setApiKey(apiKeyInput.trim());
        setHasApiKey(true);
      }
      // 刷新设置快照
      const updated = await getSettings();
      setApiKeyHint(updated.api_key_hint);
      setHasApiKey(updated.api_key_configured);
      setSettings({
        api_base: updated.api_base,
        model: updated.model,
        temperature: updated.temperature,
        timeout_ms: updated.timeout_ms,
        api_key_hint: updated.api_key_hint,
      });

      setApiKeyInput('');
      setToast({ type: 'success', msg: '设置已保存并立即生效' });
    } catch (e) {
      setToast({ type: 'error', msg: `保存失败: ${e instanceof Error ? e.message : '未知错误'}` });
    } finally {
      setSaving(false);
    }
  };

  return (
    <div className="h-full overflow-y-auto p-8">
      <div className="max-w-2xl mx-auto">
        <h1 className="text-2xl font-bold mb-1">设置</h1>
        <p className="text-gray-500 text-sm mb-8">配置 AI 模型连接参数，保存后立即生效。</p>

        {/* Toast */}
        {toast && (
          <div
            className={`mb-6 flex items-center gap-2 px-4 py-3 rounded-lg text-sm ${
              toast.type === 'success'
                ? 'bg-green-900/30 border border-green-800 text-green-300'
                : 'bg-red-900/30 border border-red-800 text-red-300'
            }`}
          >
            {toast.type === 'success' ? (
              <CheckCircle2 className="w-4 h-4 flex-shrink-0" />
            ) : (
              <AlertCircle className="w-4 h-4 flex-shrink-0" />
            )}
            {toast.msg}
          </div>
        )}

        <div className="space-y-6">
          {/* API Key */}
          <div>
            <label className="block text-sm font-medium text-gray-300 mb-1.5">API Key</label>
            {apiKeyHint && (
              <p className="text-xs text-gray-500 mb-1.5">
                当前已配置: <span className="font-mono text-gray-400">{apiKeyHint}</span>
              </p>
            )}
            <div className="relative">
              <input
                type={showKey ? 'text' : 'password'}
                value={apiKeyInput}
                onChange={(e) => setApiKeyInput(e.target.value)}
                placeholder="输入新的 API Key（留空则不修改）"
                className="w-full bg-gray-800 border border-gray-700 rounded-lg px-4 py-2.5 text-sm text-gray-100 placeholder-gray-600 focus:outline-none focus:border-blue-500 focus:ring-1 focus:ring-blue-500 pr-10"
              />
              <button
                type="button"
                onClick={() => setShowKey(!showKey)}
                className="absolute right-3 top-1/2 -translate-y-1/2 text-gray-500 hover:text-gray-300 cursor-pointer"
              >
                {showKey ? <EyeOff className="w-4 h-4" /> : <Eye className="w-4 h-4" />}
              </button>
            </div>
          </div>

          {/* API Base URL */}
          <div>
            <label className="block text-sm font-medium text-gray-300 mb-1.5">模型 API 地址</label>
            <input
              type="text"
              value={apiBase}
              onChange={(e) => setApiBase(e.target.value)}
              placeholder="https://api.openai.com/v1"
              className="w-full bg-gray-800 border border-gray-700 rounded-lg px-4 py-2.5 text-sm text-gray-100 placeholder-gray-600 focus:outline-none focus:border-blue-500 focus:ring-1 focus:ring-blue-500"
            />
          </div>

          {/* 模型名称 */}
          <div>
            <label className="block text-sm font-medium text-gray-300 mb-1.5">模型名称</label>
            <input
              type="text"
              value={model}
              onChange={(e) => setModel(e.target.value)}
              placeholder="gpt-4o-mini"
              className="w-full bg-gray-800 border border-gray-700 rounded-lg px-4 py-2.5 text-sm text-gray-100 placeholder-gray-600 focus:outline-none focus:border-blue-500 focus:ring-1 focus:ring-blue-500"
            />
            <p className="text-xs text-gray-600 mt-1">
              支持 OpenAI、DeepSeek、Qwen 等兼容 API 格式的模型
            </p>
          </div>

          {/* Temperature */}
          <div className="grid grid-cols-2 gap-4">
            <div>
              <label className="block text-sm font-medium text-gray-300 mb-1.5">
                Temperature: {temperature.toFixed(1)}
              </label>
              <input
                type="range"
                min="0"
                max="2"
                step="0.1"
                value={temperature}
                onChange={(e) => setTemperature(parseFloat(e.target.value))}
                className="w-full accent-blue-500"
              />
            </div>
            <div>
              <label className="block text-sm font-medium text-gray-300 mb-1.5">超时 (ms)</label>
              <input
                type="number"
                value={timeoutMs}
                onChange={(e) => setTimeoutMs(parseInt(e.target.value) || 30000)}
                className="w-full bg-gray-800 border border-gray-700 rounded-lg px-4 py-2.5 text-sm text-gray-100 focus:outline-none focus:border-blue-500 focus:ring-1 focus:ring-blue-500"
              />
            </div>
          </div>

          {/* 保存按钮 */}
          <button
            onClick={handleSave}
            disabled={saving}
            className="flex items-center gap-2 px-6 py-2.5 bg-blue-600 hover:bg-blue-500 disabled:bg-blue-600/50 text-white text-sm font-medium rounded-lg transition-colors cursor-pointer"
          >
            <Save className="w-4 h-4" />
            {saving ? '保存中...' : '保存设置'}
          </button>
        </div>
      </div>
    </div>
  );
}
