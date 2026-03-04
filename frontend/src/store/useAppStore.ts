import { create } from 'zustand';

interface AppState {
  /** 后端是否可达 */
  isBackendConnected: boolean;
  /** API Key 是否已配置 */
  hasApiKey: boolean;
  /** 用户在设置页输入的 API Key（内存中保留，用于 chat 请求鉴权） */
  apiKey: string;
  /** 后端配置快照 */
  settings: {
    api_base: string;
    model: string;
    temperature: number;
    timeout_ms: number;
    api_key_hint: string;
  } | null;

  setBackendConnected: (v: boolean) => void;
  setHasApiKey: (v: boolean) => void;
  setApiKey: (v: string) => void;
  setSettings: (s: AppState['settings']) => void;
}

export const useAppStore = create<AppState>((set) => ({
  isBackendConnected: false,
  hasApiKey: false,
  apiKey: '',
  settings: null,

  setBackendConnected: (v) => set({ isBackendConnected: v }),
  setHasApiKey: (v) => set({ hasApiKey: v }),
  setApiKey: (v) => set({ apiKey: v }),
  setSettings: (s) => set({ settings: s }),
}));
