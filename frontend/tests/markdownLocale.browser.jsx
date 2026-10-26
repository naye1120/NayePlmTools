import React, { useEffect, useState } from 'react'
import { createRoot } from 'react-dom/client'
import { I18nextProvider } from 'react-i18next'
import * as markdown from '../src/utils/markdown'
import i18n from '../src/i18n'
import PayloadDetailPanel from '../src/components/ui/PayloadDetailPanel'
import SettingsView from '../src/components/views/SettingsView'
import '../src/index.css'

window.__errors = []
window.addEventListener('error', event => window.__errors.push(event.message))
window.addEventListener('unhandledrejection', event => window.__errors.push(String(event.reason)))
window.markdown = markdown
window.locale = i18n
const payload = { name: 'Fixture', filename: 'fixture.elf', version: '1.0', source: 'https://github.com/fixture/payload/releases', release_notes: 'Stale mirror notes must be ignored', details_url: '/__readme', category: 'Utilities & Tools' }
function Harness() {
  const [view, setView] = useState('details')
  const [currentPayload, setCurrentPayload] = useState(payload)
  useEffect(() => { window.showView = setView; window.setFixturePayload = setCurrentPayload }, [])
  return <I18nextProvider i18n={i18n}>
    {view === 'details' ? <PayloadDetailPanel payload={currentPayload} onClose={() => setView('closed')} onInstall={() => {}} />
      : view === 'settings' ? <SettingsView config={{}} onSaveConfig={() => {}} setShowLogs={() => {}} onNavigate={() => {}} />
        : <div>Closed details</div>}
  </I18nextProvider>
}
createRoot(document.getElementById('root')).render(<Harness />)
window.fixtureReady = true
