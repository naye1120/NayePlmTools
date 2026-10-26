// Native language names remain readable on PS5 WebKit without Intl.DisplayNames.
export const LANGUAGE_NAMES = {
  'en-US': 'English', 'nl-NL': 'Nederlands', 'pl-PL': 'Polski',
  'de-DE': 'Deutsch', 'fr-FR': 'Français', 'es-ES': 'Español',
  'it-IT': 'Italiano', 'pt-BR': 'Português (Brasil)', 'ru-RU': 'Русский',
  'uk-UA': 'Українська', 'ar-SA': 'العربية', 'fa-IR': 'فارسی',
  'tr-TR': 'Türkçe', 'zh-CN': '简体中文', 'zh-TW': '繁體中文',
  'ja-JP': '日本語', 'ko-KR': '한국어', 'ro-RO': 'Română', 'th-TH': 'ไทย',
};

// Cardinal rules for the app's supported locales when older WebKit has no
// Intl.PluralRules. Keep this scoped to i18next rather than replacing global Intl.
export function fallbackCardinalRule(language) {
  const lang = language.split(/[-_]/)[0];
  const categories = ['zh', 'ja', 'ko', 'th'].includes(lang) ? ['other']
    : lang === 'ar' ? ['zero', 'one', 'two', 'few', 'many', 'other']
      : ['pl', 'ru', 'uk'].includes(lang) ? ['one', 'few', 'many', 'other']
        : ['fr', 'pt', 'es', 'it'].includes(lang) ? ['one', 'many', 'other']
          : lang === 'ro' ? ['one', 'few', 'other'] : ['one', 'other'];
  return {
    resolvedOptions: () => ({ pluralCategories: categories.slice() }),
    select(value) {
      const n = Math.abs(value), i = Math.floor(n), integer = n === i;
      if (categories.length === 1) return 'other';
      if (lang === 'ar') {
        if (n === 0) return 'zero';
        if (n === 1) return 'one';
        if (n === 2) return 'two';
        if (integer && n % 100 >= 3 && n % 100 <= 10) return 'few';
        if (integer && n % 100 >= 11 && n % 100 <= 99) return 'many';
        return 'other';
      }
      if (['pl', 'ru', 'uk'].includes(lang)) {
        if (!integer) return 'other';
        if (lang === 'pl' ? n === 1 : n % 10 === 1 && n % 100 !== 11) return 'one';
        if (n % 10 >= 2 && n % 10 <= 4 && !(n % 100 >= 12 && n % 100 <= 14)) return 'few';
        return 'many';
      }
      if (['fr', 'pt', 'es', 'it'].includes(lang)) {
        if (['fr', 'pt'].includes(lang) ? i === 0 || i === 1 : n === 1 && (lang === 'es' || integer)) return 'one';
        if (integer && n !== 0 && n % 1000000 === 0) return 'many';
        return 'other';
      }
      if (lang === 'ro') {
        if (integer && n === 1) return 'one';
        if (!integer || n === 0 || (n % 100 >= 1 && n % 100 <= 19)) return 'few';
        return 'other';
      }
      if (lang === 'fa') return i === 0 || n === 1 ? 'one' : 'other';
      if (lang === 'tr') return n === 1 ? 'one' : 'other';
      return integer && n === 1 ? 'one' : 'other';
    },
  };
}

export const legacyPluralSupport = {
  type: '3rdParty',
  init(instance) {
    if (typeof Intl !== 'undefined' && typeof Intl.PluralRules === 'function') return;
    const resolver = instance.services.pluralResolver;
    const original = resolver.getRule.bind(resolver);
    resolver.getRule = (language, options = {}) => options.ordinal
      ? original(language, options) : fallbackCardinalRule(language);
  },
};
