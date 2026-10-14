import '../polyfills.js';
import { marked } from 'marked';
import DOMPurify from 'dompurify';
import emojiMap from './emojiMap.js';
import { replaceEmojiText } from './emojiSprites.js';

marked.use({
  gfm: true,
  breaks: false,
  renderer: { image() { return ''; } },
});

// An explicit document-only policy also excludes CSS, SVG, forms and remote media.
export const MARKDOWN_POLICY = {
  ALLOWED_TAGS: ['p', 'br', 'hr', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6',
    'blockquote', 'pre', 'code', 'strong', 'em', 'del', 'ul', 'ol', 'li',
    'table', 'thead', 'tbody', 'tr', 'th', 'td', 'a'],
  ALLOWED_ATTR: ['href', 'title', 'start', 'align'],
  ALLOW_DATA_ATTR: false,
  ALLOW_ARIA_ATTR: false,
};

export function emojify(text) {
  if (!text || typeof text !== 'string') return '';
  return text.replace(/:([a-zA-Z0-9_\-+]+):/g, (match, name) => emojiMap[name] || match);
}

function escapedText(text) {
  return text.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;').replace(/'/g, '&#39;');
}

// The factory lets tests exercise unavailable/unsupported sanitizers in a real DOM.
// Emoji are generated only from sanitized text nodes, using bundled CSS assets.
// A second sanitizer pass validates the final HTML, including the trusted spans.
export function createMarkdownParser(purifier) {
  return function parseMarkdown(markdown) {
    if (!markdown || typeof markdown !== 'string') return '';
    const text = emojify(markdown);
    if (!purifier?.isSupported || typeof purifier.sanitize !== 'function') {
      return `<pre>${escapedText(text)}</pre>`;
    }
    const clean = purifier.sanitize(marked.parse(text), { ...MARKDOWN_POLICY, RETURN_DOM: true });
    replaceEmojiText(clean);
    return purifier.sanitize(clean.innerHTML, {
      ...MARKDOWN_POLICY,
      ALLOWED_TAGS: [...MARKDOWN_POLICY.ALLOWED_TAGS, 'span'],
      ALLOWED_ATTR: [...MARKDOWN_POLICY.ALLOWED_ATTR, 'class', 'role', 'aria-label'],
    });
  };
}

export const parseMarkdown = createMarkdownParser(DOMPurify);
