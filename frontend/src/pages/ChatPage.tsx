import { useState, useRef, useEffect, useCallback } from 'react';
import { Send, Loader2, StopCircle, BookOpen, AlertTriangle, BarChart3, ChevronRight } from 'lucide-react';
import { streamChat, type SSEEvent } from '../api/client';
import { useAppStore } from '../store/useAppStore';

interface Message {
  role: 'user' | 'assistant';
  content: string;
  cites?: string[];
  warnings?: string[];
  metrics?: Record<string, number>;
  streaming?: boolean;
}

/** 可折叠的告警列表 */
function WarningCollapse({ warnings }: { warnings: string[] }) {
  const [open, setOpen] = useState(false);
  return (
    <div className="rounded border border-amber-800/40 bg-amber-900/10 overflow-hidden">
      <button
        onClick={() => setOpen(!open)}
        className="w-full flex items-center gap-1.5 px-2 py-1 text-xs text-amber-400 hover:bg-amber-900/20 transition-colors cursor-pointer"
      >
        <ChevronRight className={`w-3 h-3 transition-transform ${open ? 'rotate-90' : ''}`} />
        <AlertTriangle className="w-3 h-3" />
        <span>{warnings.length} 条风控提示</span>
      </button>
      {open && (
        <div className="px-2 pb-1.5 space-y-1">
          {warnings.map((w, wi) => (
            <div
              key={wi}
              className="flex items-start gap-1.5 px-2 py-1 bg-amber-900/20 border border-amber-800/50 rounded text-xs text-amber-300"
            >
              <AlertTriangle className="w-3 h-3 mt-0.5 flex-shrink-0" />
              {w}
            </div>
          ))}
        </div>
      )}
    </div>
  );
}

export default function ChatPage() {
  const apiKey = useAppStore((s) => s.apiKey);
  const hasApiKey = useAppStore((s) => s.hasApiKey);
  const [messages, setMessages] = useState<Message[]>([]);
  const [input, setInput] = useState('');
  const [isStreaming, setIsStreaming] = useState(false);
  const abortRef = useRef<AbortController | null>(null);
  const scrollRef = useRef<HTMLDivElement>(null);

  // 自动滚动到底部
  useEffect(() => {
    if (scrollRef.current) {
      scrollRef.current.scrollTop = scrollRef.current.scrollHeight;
    }
  }, [messages]);

  const handleSend = useCallback(async () => {
    const prompt = input.trim();
    if (!prompt || isStreaming) return;

    setInput('');
    const userMsg: Message = { role: 'user', content: prompt };
    const assistantMsg: Message = { role: 'assistant', content: '', cites: [], warnings: [], streaming: true };

    setMessages((prev) => [...prev, userMsg, assistantMsg]);
    setIsStreaming(true);

    const controller = new AbortController();
    abortRef.current = controller;

    try {
      const idx = messages.length + 1; // assistant message index

      await streamChat(
        prompt,
        apiKey,
        (event: SSEEvent) => {
          setMessages((prev) => {
            const updated = [...prev];
            const msg = { ...updated[idx] };

            switch (event.type) {
              case 'token':
                msg.content += event.payload as string;
                msg.streaming = true;
                break;
              case 'cite':
                msg.cites = [...(msg.cites || []), event.payload as string];
                break;
              case 'warning':
                msg.warnings = [...(msg.warnings || []), typeof event.payload === 'string' ? event.payload : JSON.stringify(event.payload)];
                break;
              case 'metric':
                msg.metrics = event.payload as Record<string, number>;
                break;
              case 'done':
                msg.streaming = false;
                break;
            }

            updated[idx] = msg;
            return updated;
          });
        },
        controller.signal,
      );
    } catch (e) {
      if ((e as Error).name !== 'AbortError') {
        setMessages((prev) => {
          const updated = [...prev];
          const last = updated[updated.length - 1];
          if (last.role === 'assistant') {
            updated[updated.length - 1] = {
              ...last,
              content: last.content + `\n\n⚠️ 错误: ${(e as Error).message}`,
              streaming: false,
            };
          }
          return updated;
        });
      }
    } finally {
      setIsStreaming(false);
      abortRef.current = null;
    }
  }, [input, isStreaming, apiKey, messages.length]);

  const handleStop = () => {
    abortRef.current?.abort();
  };

  const handleKeyDown = (e: React.KeyboardEvent) => {
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault();
      handleSend();
    }
  };

  return (
    <div className="h-full flex flex-col">
      {/* 消息区域 */}
      <div ref={scrollRef} className="flex-1 overflow-y-auto px-4 py-6 space-y-6">
        {messages.length === 0 && (
          <div className="flex flex-col items-center justify-center h-full text-gray-600 space-y-3">
            <div className="w-16 h-16 rounded-2xl bg-gray-800 flex items-center justify-center">
              <Send className="w-7 h-7 text-gray-600" />
            </div>
            <p className="text-sm">向 FinGuard AI 提问投资相关问题</p>
            {!hasApiKey && (
              <p className="text-xs text-amber-500">⚠ 请先在设置页配置 API Key</p>
            )}
          </div>
        )}

        {messages.map((msg, i) => (
          <div key={i} className={`flex ${msg.role === 'user' ? 'justify-end' : 'justify-start'}`}>
            <div
              className={`max-w-[75%] rounded-2xl px-4 py-3 text-sm leading-relaxed ${
                msg.role === 'user'
                  ? 'bg-blue-600 text-white'
                  : 'bg-gray-800 text-gray-200'
              }`}
            >
              {/* 正文 */}
              <div className="whitespace-pre-wrap">
                {msg.content}
                {msg.streaming && <span className="cursor-blink" />}
              </div>

              {/* 附加元数据 */}
              {msg.role === 'assistant' && !msg.streaming && (
                <div className="mt-3 space-y-2">
                  {/* 引用 */}
                  {msg.cites && msg.cites.length > 0 && msg.cites[0] !== 'none' && (
                    <div className="flex flex-wrap gap-1.5">
                      {msg.cites.map((c, ci) => (
                        <span
                          key={ci}
                          className="inline-flex items-center gap-1 px-2 py-0.5 bg-blue-900/30 border border-blue-800 rounded text-xs text-blue-300"
                        >
                          <BookOpen className="w-3 h-3" />
                          {c}
                        </span>
                      ))}
                    </div>
                  )}

                  {/* 告警（可折叠） */}
                  {msg.warnings && msg.warnings.length > 0 && msg.warnings[0] !== 'none' && (
                    <WarningCollapse warnings={msg.warnings} />
                  )}

                  {/* 指标 */}
                  {msg.metrics && (
                    <div className="flex items-center gap-3 text-xs text-gray-500">
                      <BarChart3 className="w-3 h-3" />
                      {Object.entries(msg.metrics).map(([k, v]) => (
                        <span key={k}>
                          {k}: {v}
                        </span>
                      ))}
                    </div>
                  )}
                </div>
              )}
            </div>
          </div>
        ))}
      </div>

      {/* 输入区域 */}
      <div className="border-t border-gray-800 px-4 py-3 bg-gray-900/50">
        <div className="max-w-4xl mx-auto flex items-end gap-3">
          <textarea
            value={input}
            onChange={(e) => setInput(e.target.value)}
            onKeyDown={handleKeyDown}
            placeholder="输入你的投资问题... (Enter 发送, Shift+Enter 换行)"
            rows={1}
            className="flex-1 bg-gray-800 border border-gray-700 rounded-xl px-4 py-3 text-sm text-gray-100 placeholder-gray-600 resize-none focus:outline-none focus:border-blue-500 focus:ring-1 focus:ring-blue-500 max-h-32"
            style={{ minHeight: '44px' }}
          />
          {isStreaming ? (
            <button
              onClick={handleStop}
              className="flex-shrink-0 p-3 bg-red-600 hover:bg-red-500 text-white rounded-xl transition-colors cursor-pointer"
            >
              <StopCircle className="w-5 h-5" />
            </button>
          ) : (
            <button
              onClick={handleSend}
              disabled={!input.trim()}
              className="flex-shrink-0 p-3 bg-blue-600 hover:bg-blue-500 disabled:bg-gray-700 disabled:text-gray-500 text-white rounded-xl transition-colors cursor-pointer"
            >
              {isStreaming ? <Loader2 className="w-5 h-5 animate-spin" /> : <Send className="w-5 h-5" />}
            </button>
          )}
        </div>
      </div>
    </div>
  );
}
