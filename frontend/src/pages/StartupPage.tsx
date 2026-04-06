import { useEffect, useRef, useState } from 'react';
import { Shield, Loader2, AlertCircle, CheckCircle2 } from 'lucide-react';
import { checkHealth, getSettings, postSettings } from '../api/client';
import { useAppStore } from '../store/useAppStore';
import { isTauri, loadPersistedSettings } from '../api/store';

type Status = 'waiting' | 'checking' | 'connected' | 'failed';

export default function StartupPage() {
  const [status, setStatus] = useState<Status>(isTauri() ? 'waiting' : 'checking');
  const [statusMsg, setStatusMsg] = useState<string>(
    isTauri() ? '正在启动后端服务...' : '正在连接后端服务...'
  );
  const [retryCount, setRetryCount] = useState(0);
  const pollTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  const setBackendConnected = useAppStore((s) => s.setBackendConnected);
  const setHasApiKey = useAppStore((s) => s.setHasApiKey);
  const setSettings = useAppStore((s) => s.setSettings);

  // ── 健康检查 + 启动流程 ──
  useEffect(() => {
    let cancelled = false;

    const tryConnect = async () => {
      if (cancelled) return;
      setStatus('checking');
      setStatusMsg('正在连接后端服务...');

      const ok = await checkHealth();
      if (cancelled) return;

      if (ok) {
        setStatus('connected');
        setStatusMsg('连接成功，正在进入...');

        // Tauri 环境：将持久化配置注入后端
        if (isTauri()) {
          try {
            const persisted = await loadPersistedSettings();
            if (
              persisted &&
              (persisted.api_key || persisted.api_base || persisted.valuecell_db_profile)
            ) {
              await postSettings({
                api_key: persisted.api_key,
                api_base: persisted.api_base,
                model: persisted.model,
                temperature: persisted.temperature,
                timeout_ms: persisted.timeout_ms,
                valuecell_db_profile: persisted.valuecell_db_profile,
              });
            }
          } catch {
            // 注入失败不阻止进入
          }
        }

        // 获取最新设置
        try {
          const settings = await getSettings();
          const dbProfiles = Array.isArray(settings.valuecell_db_profiles)
            ? settings.valuecell_db_profiles
            : [];
          if (!cancelled) {
            setSettings({
              api_base: settings.api_base,
              model: settings.model,
              temperature: settings.temperature,
              timeout_ms: settings.timeout_ms,
              api_key_hint: settings.api_key_hint,
              valuecell_db_profile: settings.valuecell_db_profile,
              valuecell_db_profile_active: settings.valuecell_db_profile_active,
              valuecell_db_profile_label: settings.valuecell_db_profile_label,
              valuecell_db_path_hint: settings.valuecell_db_path_hint,
              valuecell_db_profiles: dbProfiles,
            });
            setHasApiKey(settings.api_key_configured);
          }
        } catch {
          // 获取设置失败不阻止进入
        }

        setTimeout(() => {
          if (!cancelled) setBackendConnected(true);
        }, 600);
      } else {
        if (isTauri()) {
          // Tauri 模式：自动重试（sidecar 可能还在启动中）
          pollTimerRef.current = setTimeout(() => tryConnect(), 1000);
        } else {
          setStatus('failed');
          setStatusMsg('无法连接到后端服务');
        }
      }
    };

    tryConnect();
    return () => {
      cancelled = true;
      if (pollTimerRef.current) clearTimeout(pollTimerRef.current);
    };
  }, [retryCount, setBackendConnected, setHasApiKey, setSettings]);

  // ── Tauri sidecar 状态事件监听 ──
  useEffect(() => {
    if (!isTauri()) return;
    let unlisten: (() => void) | null = null;

    import('@tauri-apps/api/event').then(({ listen }) => {
      listen<string>('sidecar-status', (event) => {
        const s = event.payload;
        if (s === 'starting') {
          setStatus('waiting');
          setStatusMsg('正在启动后端服务...');
        } else if (s === 'stopped') {
          setStatus('failed');
          setStatusMsg('后端服务意外停止');
        } else if (s.startsWith('error:')) {
          setStatus('failed');
          setStatusMsg(`启动失败：${s.slice(6).trim()}`);
        }
      }).then((fn) => { unlisten = fn; });
    });

    return () => { unlisten?.(); };
  }, []);

  return (
    <div className="min-h-screen flex items-center justify-center bg-gray-950">
      <div className="text-center space-y-6">
        {/* Logo */}
        <div className="flex justify-center">
          <div className="w-20 h-20 rounded-2xl bg-blue-600/20 flex items-center justify-center">
            <Shield className="w-10 h-10 text-blue-400" />
          </div>
        </div>

        <div>
          <h1 className="text-3xl font-bold text-white">FinGuard</h1>
          <p className="text-gray-500 mt-1">AI 资产配置中台</p>
        </div>

        {/* 状态 */}
        <div className="flex items-center justify-center gap-2 text-sm">
          {(status === 'waiting' || status === 'checking') && (
            <>
              <Loader2 className="w-4 h-4 animate-spin text-blue-400" />
              <span className="text-gray-400">{statusMsg}</span>
            </>
          )}
          {status === 'connected' && (
            <>
              <CheckCircle2 className="w-4 h-4 text-green-400" />
              <span className="text-green-400">{statusMsg}</span>
            </>
          )}
          {status === 'failed' && (
            <>
              <AlertCircle className="w-4 h-4 text-red-400" />
              <span className="text-red-400">{statusMsg}</span>
            </>
          )}
        </div>

        {status === 'failed' && (
          <div className="space-y-3">
            <p className="text-xs text-gray-500">
              {isTauri()
                ? '后端服务启动失败，请重试或检查日志'
                : '请确认 finguard.exe 正在运行（端口 8080）'}
            </p>
            <button
              onClick={() => setRetryCount((c) => c + 1)}
              className="px-4 py-2 bg-blue-600 hover:bg-blue-500 text-white text-sm rounded-lg transition-colors cursor-pointer"
            >
              重试连接
            </button>
          </div>
        )}
      </div>
    </div>
  );
}
