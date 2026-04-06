/**
 * 配置持久化工具
 * 在 Tauri 环境下使用 tauri-plugin-store，浏览器环境下使用 localStorage 降级。
 */

export interface PersistedSettings {
  api_base: string;
  model: string;
  temperature: number;
  timeout_ms: number;
  api_key: string;
  valuecell_db_profile: string;
  valuecell_state?: any;
  plan_state?: any;
}

const STORE_FILE = 'settings.json';
const LS_KEY = 'finguard_settings';

/** 检测当前是否运行在 Tauri 容器内 */
export function isTauri(): boolean {
  return typeof window !== 'undefined' && '__TAURI__' in window;
}

/** 读取持久化配置。返回 null 表示尚未配置。 */
export async function loadPersistedSettings(): Promise<Partial<PersistedSettings> | null> {
  if (isTauri()) {
    try {
      const { load } = await import('@tauri-apps/plugin-store');
      const store = await load(STORE_FILE, { autoSave: true, defaults: {} });
      const raw = await store.get<Partial<PersistedSettings>>('settings');
      return raw ?? null;
    } catch {
      return null;
    }
  } else {
    // 浏览器 / Vite dev 环境降级到 localStorage
    try {
      const raw = localStorage.getItem(LS_KEY);
      return raw ? JSON.parse(raw) : null;
    } catch {
      return null;
    }
  }
}

/** 写入持久化配置 */
export async function savePersistedSettings(
  settings: Partial<PersistedSettings>
): Promise<void> {
  if (isTauri()) {
    try {
      const { load } = await import('@tauri-apps/plugin-store');
      const store = await load(STORE_FILE, { autoSave: true, defaults: {} });
      await store.set('settings', settings);
      await store.save();
    } catch (e) {
      console.warn('[store] save failed:', e);
    }
  } else {
    try {
      const prev = localStorage.getItem(LS_KEY);
      const merged = { ...(prev ? JSON.parse(prev) : {}), ...settings };
      localStorage.setItem(LS_KEY, JSON.stringify(merged));
    } catch (e) {
      console.warn('[store] localStorage save failed:', e);
    }
  }
}

/** 清除持久化配置 */
export async function clearPersistedSettings(): Promise<void> {
  if (isTauri()) {
    try {
      const { load } = await import('@tauri-apps/plugin-store');
      const store = await load(STORE_FILE, { autoSave: true, defaults: {} });
      await store.delete('settings');
      await store.save();
    } catch {}
  } else {
    localStorage.removeItem(LS_KEY);
  }
}

/** 保存页面状态到持久化存储 */
export async function savePageState(
  key: 'valuecell_state' | 'plan_state',
  state: any
): Promise<void> {
  const current = await loadPersistedSettings();
  await savePersistedSettings({
    ...current,
    [key]: state,
  });
}

/** 从持久化存储加载页面状态 */
export async function loadPageState<T>(
  key: 'valuecell_state' | 'plan_state'
): Promise<T | null> {
  const settings = await loadPersistedSettings();
  return (settings?.[key] as T) ?? null;
}
