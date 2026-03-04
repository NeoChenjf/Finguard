/**
 * FinGuard 后端 API 客户端
 * 所有后端请求统一从这里发出
 */

// Tauri 打包环境中没有 Vite proxy，需直接请求后端地址；
// 开发环境（Vite dev server）使用空字符串走 proxy。
// 注意：Tauri v2 注入的全局变量是 __TAURI_INTERNALS__（非 v1 的 __TAURI__）
const API_BASE =
  typeof window !== 'undefined' && '__TAURI_INTERNALS__' in window
    ? 'http://127.0.0.1:8080'
    : '';

// ── 类型定义 ──

export interface SettingsResponse {
  api_base: string;
  model: string;
  temperature: number;
  timeout_ms: number;
  api_key_configured: boolean;
  api_key_hint: string;
}

export interface PlanRequest {
  profile: {
    age: number;
    investor_type: 'novice' | 'experienced' | 'professional';
    experience_years: string;
    annualized_return: string;
    beat_sp500_10y: string;
    individual_stock_percent: number;
  };
  portfolio: Array<{ symbol: string; weight: number }>;
  constraints: {
    min_single_asset?: number;
  };
}

export interface PlanResponse {
  proposed_portfolio: Record<string, number>;
  risk_report: {
    status: string;
    triggered_rules: string[];
  };
  rationale: string;
  rebalancing_actions: string[];
}

export interface SSEEvent {
  type: 'token' | 'cite' | 'metric' | 'warning' | 'done';
  payload: string | Record<string, number>;
}

// ── 健康检查 ──

export async function checkHealth(): Promise<boolean> {
  try {
    const res = await fetch(`${API_BASE}/health`, { signal: AbortSignal.timeout(3000) });
    if (!res.ok) return false;
    const data = await res.json();
    return data.status === 'ok';
  } catch {
    return false;
  }
}

// ── 设置 ──

export async function getSettings(): Promise<SettingsResponse> {
  const res = await fetch(`${API_BASE}/api/v1/settings`);
  if (!res.ok) throw new Error(`GET /settings failed: ${res.status}`);
  return res.json();
}

export async function postSettings(body: Record<string, unknown>): Promise<{ status: string; message: string }> {
  const res = await fetch(`${API_BASE}/api/v1/settings`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  });
  if (!res.ok) {
    const err = await res.json().catch(() => ({}));
    throw new Error(err.error || `POST /settings failed: ${res.status}`);
  }
  return res.json();
}

// ── 配置建议 ──

export async function postPlan(body: PlanRequest): Promise<PlanResponse> {
  const res = await fetch(`${API_BASE}/api/v1/plan`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  });
  if (!res.ok) {
    const err = await res.json().catch(() => ({}));
    throw new Error(err.error || `POST /plan failed: ${res.status}`);
  }
  return res.json();
}

// ── Value Cell 量化分析 ──

export interface ValueCellRequest {
  symbol: string;
}

export interface ValueCellResponse {
  symbol: string;
  current_pe: number;
  historical_pe_mean: number;
  current_peg: number;
  price_to_book: number;
  earnings_growth: number;
  pe_history_quarters_used: number;
  safety_margin: boolean;
  safety_margin_reason: string;
  qualitative_score: number;
  qualitative_analysis: {
    moat: string;
    management: string;
    business_model: string;
  };
  investment_conclusion: string;
  data_source: string;
  analysis_time_ms: number;
  data_warning?: string;
  search_warning?: string;
  llm_warning?: string;
}

export async function postValueCell(symbol: string): Promise<ValueCellResponse> {
  const res = await fetch(`${API_BASE}/api/v1/valuecell`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ symbol }),
  });
  if (!res.ok) {
    const err = await res.json().catch(() => ({}));
    throw new Error(err.message || err.error || `POST /valuecell failed: ${res.status}`);
  }
  return res.json();
}

// ── 流式问答 (SSE over POST) ──

export async function streamChat(
  prompt: string,
  apiKey: string,
  onEvent: (event: SSEEvent) => void,
  signal?: AbortSignal,
): Promise<void> {
  const headers: Record<string, string> = {
      'Content-Type': 'application/json',
    };
    if (apiKey) {
      headers['X-API-Key'] = apiKey;
    }
  const res = await fetch(`${API_BASE}/api/v1/chat/stream`, {
    method: 'POST',
    headers,
    body: JSON.stringify({ prompt }),
    signal,
  });

  if (!res.ok) {
    const err = await res.json().catch(() => ({}));
    throw new Error(err.error || `POST /chat/stream failed: ${res.status}`);
  }

  const reader = res.body?.getReader();
  if (!reader) throw new Error('ReadableStream not supported');

  const decoder = new TextDecoder();
  let buffer = '';

  while (true) {
    const { done, value } = await reader.read();
    if (done) break;

    buffer += decoder.decode(value, { stream: true });

    // 按 SSE 格式分割事件：每个事件以 \n\n 分隔
    const parts = buffer.split('\n\n');
    buffer = parts.pop() || ''; // 最后一个可能是不完整的

    for (const part of parts) {
      const line = part.trim();
      if (!line.startsWith('data: ')) continue;
      const json = line.slice(6); // 去掉 'data: '
      try {
        const event: SSEEvent = JSON.parse(json);
        onEvent(event);
      } catch {
        // 忽略解析错误
      }
    }
  }
}
