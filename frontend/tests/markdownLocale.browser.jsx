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
const notes = '# Release :pig: 👩🏽‍💻 🇵🇱 1️⃣\n\n[Safe link](https://example.com/docs) and [anchor](#section)\n\n| Name | Value |\n| --- | --- |\n| Test | **works** |'
const payload = { name: 'Fixture', filename: 'fixture.elf', release_notes: notes, details_url: '/__readme', category: 'Utilities & Tools' }
function Harness() {
  const [view, setView] = useState('details')
  useEffect(() => { window.showView = setView }, [])
  return <I18nextProvider i18n={i18n}>
    {view === 'details' ? <PayloadDetailPanel payload={payload} onClose={() => setView('closed')} onInstall={() => {}} />
      : view === 'settings' ? <SettingsView config={{}} onSaveConfig={() => {}} setShowLogs={() => {}} onNavigate={() => {}} />
        : <div>Closed details</div>}
  </I18nextProvider>
}
createRoot(document.getElementById('root')).render(<Harness />)
window.fixtureReady = true
