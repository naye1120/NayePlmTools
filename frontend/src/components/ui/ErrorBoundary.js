import React from 'react';

class ErrorBoundary extends React.Component {
  constructor(props) {
    super(props);
    this.state = {
      hasError: false,
      error: null,
      errorInfo: null,
    };
    this.handleReload = this.handleReload.bind(this);
    this.handleReset = this.handleReset.bind(this);
    this.handleDismiss = this.handleDismiss.bind(this);
  }

  static getDerivedStateFromError(error) {
    return { hasError: true, error };
  }

  componentDidCatch(error, errorInfo) {
    console.error('ErrorBoundary caught an error:', error, errorInfo);
    this.setState({ errorInfo });
    if (this.props.onError) {
      try {
        this.props.onError(error, errorInfo);
      } catch {
        /* ignore */
      }
    }
  }

  handleReload() {
    window.location.reload();
  }

  handleReset() {
    try {
      localStorage.clear();
      sessionStorage.clear();
    } catch {
      /* ignore */
    }
    window.location.reload();
  }

  handleDismiss() {
    this.setState({ hasError: false, error: null, errorInfo: null });
  }

  render() {
    if (this.state.hasError) {
      const { error, errorInfo } = this.state;
      const errorMessage = error?.message || String(error || 'Unknown error');
      const errorStack = error?.stack || '';
      const componentStack = errorInfo?.componentStack || '';

      return React.createElement(
        'div',
        { className: 'min-h-screen bg-[#08080a] text-zinc-100 font-mono p-6 sm:p-10 flex flex-col items-center justify-center z-[999999] relative' },
        React.createElement(
          'div',
          { className: 'w-full max-w-4xl bg-red-950/20 border-2 border-red-500/50 rounded-3xl p-6 sm:p-8 shadow-2xl space-y-6' },
          React.createElement(
            'div',
            { className: 'flex items-center space-x-3 text-red-500' },
            React.createElement('h1', { className: 'text-xl sm:text-2xl font-bold tracking-tight text-white uppercase' }, this.props.title || 'Runtime Error Detected')
          ),
          React.createElement('p', { className: 'text-zinc-300 text-sm leading-relaxed' }, 'A runtime exception was caught by the UI ErrorBoundary. This screen prevents the PS5 browser from rendering a blank/black screen.'),
          React.createElement(
            'div',
            { className: 'space-y-2' },
            React.createElement('span', { className: 'text-xs uppercase tracking-wider text-red-400 font-bold' }, 'Error Message:'),
            React.createElement('div', { className: 'bg-black/60 border border-red-500/30 rounded-xl p-4 text-red-200 text-xs sm:text-sm font-mono break-all whitespace-pre-wrap select-text max-h-40 overflow-y-auto' }, errorMessage)
          ),
          (errorStack || componentStack) ? React.createElement(
            'div',
            { className: 'space-y-2' },
            React.createElement('span', { className: 'text-xs uppercase tracking-wider text-zinc-400 font-bold' }, 'Stack Trace:'),
            React.createElement('div', { className: 'bg-black/60 border border-white/10 rounded-xl p-4 text-zinc-400 text-[11px] sm:text-xs font-mono break-all whitespace-pre-wrap select-text max-h-60 overflow-y-auto' }, `${errorStack}${componentStack ? `\n\nComponent Stack:${componentStack}` : ''}`)
          ) : null,
          React.createElement(
            'div',
            { className: 'flex flex-wrap gap-3 pt-2' },
            React.createElement('button', { type: 'button', onClick: this.handleReload, className: 'px-6 py-3 rounded-xl bg-red-600 hover:bg-red-500 text-white font-bold text-sm uppercase tracking-wider transition-all cursor-pointer' }, 'Reload Page'),
            React.createElement('button', { type: 'button', onClick: this.handleReset, className: 'px-6 py-3 rounded-xl bg-zinc-800 hover:bg-zinc-700 text-zinc-200 font-bold text-sm uppercase tracking-wider transition-all border border-white/10 cursor-pointer' }, 'Reset Storage & Reload'),
            this.props.canDismiss ? React.createElement('button', { type: 'button', onClick: this.handleDismiss, className: 'px-6 py-3 rounded-xl bg-white/5 hover:bg-white/10 text-zinc-400 font-bold text-sm uppercase tracking-wider transition-all cursor-pointer' }, 'Dismiss') : null
          )
        )
      );
    }

    return this.props.children;
  }
}

export default ErrorBoundary;
