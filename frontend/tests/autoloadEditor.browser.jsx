// Browser-only regression fixture: real component, controlled config transport.
import React, { useEffect, useState } from 'react'
import { createRoot } from 'react-dom/client'
import { createInstance } from 'i18next'
import { I18nextProvider } from 'react-i18next'
import AutoloadView from '../src/components/views/AutoloadView'
import { mutationFetch } from '../src/utils/mutationRequest'

const i18n = createInstance()
await i18n.init({ lng: 'en', resources: { en: { translation: {} } }, keySeparator: false })
function Harness() {
  const [config, setConfig] = useState({})
  const [mounted, setMounted] = useState(true)
  useEffect(() => {
    fetch('/__editor_config').then(async response => {
      if (response.ok) setConfig(await response.json())
    })
  }, [])
  window.editor = { mount: setMounted, config: setConfig }
  return mounted ? <AutoloadView config={config} items={[]} payloads={[]}
    onToast={() => {}} onSaveConfig={async update => {
      const response = await mutationFetch('/__editor_save', {
        headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(update),
      })
      return response.ok
    }} /> : <div>Other section</div>
}
createRoot(document.getElementById('root')).render(<I18nextProvider i18n={i18n}><Harness /></I18nextProvider>)
