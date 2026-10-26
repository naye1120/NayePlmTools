// Actual components with controlled transport; never touches a PS5 loader.
import React, { StrictMode, useEffect, useState } from 'react'
import { createRoot } from 'react-dom/client'
import { createInstance } from 'i18next'
import { I18nextProvider } from 'react-i18next'
import AutoloadView from '../src/components/views/AutoloadView'
import AutoloadOverlay from '../src/components/views/AutoloadOverlay'
import ActiveProcessesView from '../src/components/views/ActiveProcessesView'
import LogViewer from '../src/components/views/LogViewer'
import App from '../src/App'
import { RepositoryProvider } from '../src/contexts/RepositoryContext'
import { mutationFetch } from '../src/utils/mutationRequest'
import { createRequestScope } from '../src/utils/requestScope'
import '../src/index.css'

const i18n = createInstance()
await i18n.init({ lng: 'en', resources: { en: { translation: {} } }, keySeparator: false })
window.__unhandledErrors = []
window.__fetchRequests = []
const transport = window.fetch.bind(window)
window.fetch = (url, options) => { window.__fetchRequests.push(String(url)); return transport(url, options) }
const interval = window.setInterval.bind(window)
window.setInterval = (callback, ms, ...args) => {
  if (ms === 15000) window.__pollProcesses = callback
  return interval(callback, ms, ...args)
}
window.addEventListener('unhandledrejection', event => { window.__unhandledErrors.push(String(event.reason)); event.preventDefault() })
window.addEventListener('error', event => window.__unhandledErrors.push(event.message))
const items = [
  { slot: 'local/kept-a', filename: 'a.elf', name: 'A', kind: 'local' },
  { slot: 'local/kept-b', filename: 'b.elf', name: 'B', kind: 'local' },
  { slot: 'local/next', filename: 'next.elf', name: 'Next', kind: 'local' },
]
const overlayItems = [...items, { slot: 'repo/demo@bbabc123', id: 'demo', filename: 'demo.elf', version: '1.1', source_url: 'https://mirror.invalid/payloads.json' }]
const finishOverlay = () => { window.__overlayFinishes = (window.__overlayFinishes || 0) + 1 }
function Harness() {
  const [config, setConfig] = useState({}), [mounted, setMounted] = useState(true)
  const [view, setView] = useState('autoload'), [status, setStatus] = useState({ state: 'countdown', remaining: 5, delay: 5 })
  const [logs, setLogs] = useState(Array.from({ length: 100 }, (_, i) => `line ${i}`))
  const [overlayItemsLoading, setOverlayItemsLoading] = useState(false)
  useEffect(() => {
    const scope = createRequestScope()
    fetch('/__editor_config', { signal: scope.signal }).then(async response => {
      const value = await response.json()
      if (response.ok && !scope.aborted) setConfig(value)
    }).catch(() => {})
    return () => scope.abort()
  }, [])
  window.editor = { mount: setMounted, config: setConfig, show: setView, status: setStatus, logs: setLogs, itemsLoading: setOverlayItemsLoading }
  if (!mounted) return <div>Other section</div>
  if (view === 'overlay') return <RepositoryProvider><AutoloadOverlay autoReturn={config.AUTOLOAD_RETURN_TO_DASHBOARD === true} status={status} items={overlayItemsLoading ? [] : overlayItems} itemsLoading={overlayItemsLoading} onCancel={() => {}} onFinish={finishOverlay} /></RepositoryProvider>
  if (view === 'logs') return <div style={{ height: 300 }}><LogViewer logs={logs} /></div>
  if (view === 'processes') return <ActiveProcessesView addToast={() => {}} showConfirm={(_, __, callback) => callback()} />
  if (view === 'app') return <RepositoryProvider><App /></RepositoryProvider>
  return <div style={{ height: 650 }}><AutoloadView config={config} items={items} onToast={() => {}}
    onSaveConfig={async update => {
      if (window.__rejectSave) throw new Error('Injected save failure')
      return (await mutationFetch('/__editor_save', { headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(update) })).ok
    }} /></div>
}
createRoot(document.getElementById('root')).render(<StrictMode><I18nextProvider i18n={i18n}><Harness /></I18nextProvider></StrictMode>)
