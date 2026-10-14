import React, { useEffect } from 'react'
import { createRoot } from 'react-dom/client'
import { RepositoryProvider, useRepository } from '../src/contexts/RepositoryContext'
import SettingsView from '../src/components/views/SettingsView'
import PayloadIcon from '../src/components/ui/PayloadIcon'
import '../src/i18n'
import '../src/index.css'
window.__errors = []
window.addEventListener('error', event => window.__errors.push(event.message))
window.addEventListener('unhandledrejection', event => window.__errors.push(String(event.reason)))
function Harness() {
  const repository = useRepository()
  useEffect(() => { window.refreshIcons = repository.fetchRemote }, [repository.fetchRemote])
  return <>
    <div data-testid="icons">{repository.sources.flatMap(source => source.payloads.map(payload =>
      <div key={`${source.id}/${payload.id}`} data-key={payload.icon_key}>
        <PayloadIcon icon={payload.icon} name={payload.name} className="w-12 h-12" />
      </div>))}</div>
    <SettingsView config={{}} onSaveConfig={() => {}} setShowLogs={() => {}} onNavigate={() => {}} onClearCache={repository.clearCache} />
  </>
}
createRoot(document.getElementById('root')).render(<RepositoryProvider><Harness /></RepositoryProvider>)
window.fixtureReady = true
