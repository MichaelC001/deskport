/* UI-only localization adapter. No protocol values or authentication state. */
(() => {
  'use strict';
  const resources = window.DeskPortUIResources;
  const registry = resources.languages;
  const storageKey = 'deskport.language';
  const readPreference = () => { try { return localStorage.getItem(storageKey) || ''; } catch (_) { return ''; } };
  function match(tag) {
    const parts = String(tag || '').replaceAll('_', '-').toLowerCase().split('-');
    if (parts[0] === 'in') parts[0] = 'id';
    if (parts[0] === 'zh') {
      return parts.includes('hant') || (!parts.includes('hans') && parts.some(p => ['tw', 'hk', 'mo'].includes(p))) ? 'zh-Hant' : 'zh-Hans';
    }
    return registry.find(language => language.code.toLowerCase() === parts.join('-'))?.code
      || registry.find(language => language.code === parts[0])?.code || '';
  }
  function resolve(preferred, browserLanguages = navigator.languages || [navigator.language]) {
    return match(preferred) || browserLanguages.map(match).find(Boolean) || 'en';
  }
  let preference = readPreference();
  let locale = resolve(preference);
  const t = (source, ...args) => {
    const cell = resources.strings[locale]?.[source];
    const text = typeof cell === 'string' && cell ? cell : source;
    // One substitution pass: a value containing %2 is data, not another template.
    return text.replace(/%([1-9]\d*)/g, (placeholder, number) =>
      Number(number) <= args.length ? `\u2068${String(args[Number(number) - 1])}\u2069` : placeholder);
  };
  function message(value) {
    if (value && typeof value === 'object') {
      if ('literal' in value) return String(value.literal);
      return t(value.source, ...(value.args || []).map(message));
    }
    return t(String(value ?? ''));
  }
  function render() {
    document.documentElement.lang = locale;
    document.documentElement.dir = registry.find(language => language.code === locale).direction;
    for (const element of document.querySelectorAll('[data-i18n]')) element.textContent = t(element.dataset.i18n);
    for (const attr of ['placeholder', 'title', 'aria-label']) {
      for (const element of document.querySelectorAll(`[data-i18n-${attr}]`)) element.setAttribute(attr, t(element.getAttribute(`data-i18n-${attr}`)));
    }
    const picker = document.getElementById('language');
    if (picker) {
      if (!picker.options.length) {
        picker.add(new Option('', ''));
        for (const language of registry) {
          const option = new Option(language.name, language.code);
          option.lang = language.code;
          option.dir = language.direction;
          picker.add(option);
        }
      }
      picker.options[0].textContent = t('System default');
      picker.value = match(preference) ? match(preference) : '';
    }
  }
  function select(value) {
    preference = match(value);
    try {
      if (preference) localStorage.setItem(storageKey, preference);
      else localStorage.removeItem(storageKey);
    } catch (_) { /* This visit still supports switching without storage. */ }
    locale = resolve(preference);
    render();
    window.dispatchEvent(new Event('deskportlanguagechange'));
  }
  window.DeskPortI18n = { t, message, match, resolve, select, get locale() { return locale; } };
  render();
  document.getElementById('language')?.addEventListener('change', event => select(event.target.value));
  window.addEventListener('languagechange', () => { if (!match(preference)) select(''); });
  window.addEventListener('storage', event => {
    if (event.key !== storageKey && event.key !== null) return;
    preference = readPreference(); locale = resolve(preference); render();
    window.dispatchEvent(new Event('deskportlanguagechange'));
  });
})();
