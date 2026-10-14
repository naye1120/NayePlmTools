import i18n from 'i18next';
import { initReactI18next } from 'react-i18next';
import LanguageDetector from 'i18next-browser-languagedetector';
import { legacyPluralSupport } from './utils/localeSupport';

// Import all locales dynamically so they are bundled by Vite
const resources = {};
const modules = import.meta.glob('./locales/*.json', { eager: true });
for (const path in modules) {
  const language = path.match(/\/([^/]+)\.json$/)[1];
  resources[language] = { translation: modules[path].default || modules[path] };
}

// [CN-FORK] Merge fork-local translation overlays kept OUTSIDE the Crowdin
// submodule (frontend/src/locales-overlay). They can add new keys (theme, nav
// groups, ...) and fine-tune existing ones without ever conflicting with
// upstream translation syncs.
const overlayModules = import.meta.glob('./locales-overlay/*.json', { eager: true });
for (const path in overlayModules) {
  const language = path.match(/\/([^/]+)\.json$/)[1];
  const overlay = overlayModules[path].default || overlayModules[path];
  const base = resources[language] ? resources[language].translation : {};
  resources[language] = { translation: { ...base, ...overlay } };
}

i18n
  // detect user language
  // learn more: https://github.com/i18next/i18next-browser-languageDetector
  .use(LanguageDetector)
  // pass the i18n instance to react-i18next.
  .use(initReactI18next)
  .use(legacyPluralSupport)
  // init i18next
  // for all options read: https://www.i18next.com/overview/configuration-options
  .init({
    resources,
    fallbackLng: 'en-US',
    supportedLngs: Object.keys(resources),
    
    // Default to detected browser language on first load, and load from localStorage if previously selected
    detection: {
      order: ['localStorage', 'navigator'],
      // Only explicit selections are persisted by changeLanguagePreference.
      caches: []
    },
    
    // We use the 'natural' naming strategy: keys are strings in English
    // So if the key is missing in translation file, it will just use the key itself.
    keySeparator: false, 

    interpolation: {
      escapeValue: false, // not needed for react as it escapes by default
    }
  });

export default i18n;
