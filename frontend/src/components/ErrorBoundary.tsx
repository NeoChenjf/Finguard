import { Component, type ErrorInfo, type ReactNode } from 'react';

interface Props {
  children: ReactNode;
}

interface State {
  hasError: boolean;
  message: string;
}

export default class ErrorBoundary extends Component<Props, State> {
  state: State = {
    hasError: false,
    message: '',
  };

  static getDerivedStateFromError(error: Error): State {
    return {
      hasError: true,
      message: error?.stack || error?.message || 'unknown_error',
    };
  }

  componentDidCatch(error: Error, info: ErrorInfo) {
    console.error('[FinGuard][ErrorBoundary]', error, info);
  }

  render() {
    if (this.state.hasError) {
      return (
        <div className="min-h-screen bg-gray-950 text-gray-100 flex items-center justify-center p-6">
          <div className="max-w-3xl w-full rounded-2xl border border-red-800/60 bg-red-950/20 p-6 space-y-4">
            <h1 className="text-lg font-semibold text-red-300">页面渲染失败</h1>
            <p className="text-sm text-red-200">
              ValueCell 前端发生了运行时错误，已阻止整页黑屏。请把下面错误信息发给我继续排查。
            </p>
            <pre className="text-xs whitespace-pre-wrap break-all bg-black/30 rounded-lg p-4 text-red-100 overflow-auto">
              {this.state.message}
            </pre>
          </div>
        </div>
      );
    }

    return this.props.children;
  }
}
