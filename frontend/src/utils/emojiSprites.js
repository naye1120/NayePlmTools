import identifiers from '../assets/emoji/sprites.js';

// Match the longest bundled sequence, including flags, skin tones and ZWJ/tag
// sequences. Variation selectors don't change which Twemoji artwork is used.
const root = new Map();
const sheets = new WeakMap();

function ensureSpriteRule(sprite) {
  // Only generated numeric indices reach this function. Keep arbitrary styles
  // out of markdown; add the few position rules actually used to the app head.
  if (typeof document === 'undefined') return;
  let entry = sheets.get(document);
  if (!entry) {
    const style = document.createElement('style');
    style.setAttribute('data-emoji-positions', '');
    document.head.appendChild(style);
    entry = { sheet: style.sheet, seen: new Set() };
    sheets.set(document, entry);
  }
  if (entry.seen.has(sprite)) return;
  const rows = Math.ceil(identifiers.length / 64);
  const x = (sprite % 64) / 63 * 100;
  const y = Math.floor(sprite / 64) / (rows - 1) * 100;
  entry.sheet.insertRule(`.markdown-body .emoji-${sprite}{background-position:${x.toFixed(8)}% ${y.toFixed(8)}%}`, entry.sheet.cssRules.length);
  entry.seen.add(sprite);
}
identifiers.forEach((identifier, index) => {
  let branch = root;
  for (const point of identifier.split('-').filter(hex => hex !== 'fe0f')) {
    const character = String.fromCodePoint(parseInt(point, 16));
    if (!branch.has(character)) branch.set(character, new Map());
    branch = branch.get(character);
  }
  branch.set('', index);
});

export function emojiSegments(text) {
  const characters = Array.from(text), segments = [];
  let plain = '';
  for (let start = 0; start < characters.length;) {
    let branch = root, end = start, matchEnd = start, sprite;
    while (end < characters.length) {
      const character = characters[end];
      if (character !== '\uFE0F') {
        branch = branch.get(character);
        if (!branch) break;
      } else if (end === start) break;
      end++;
      if (branch.has('')) { sprite = branch.get(''); matchEnd = end; }
    }
    if (sprite === undefined) { plain += characters[start++]; continue; }
    if (plain) { segments.push({ text: plain }); plain = ''; }
    segments.push({ text: characters.slice(start, matchEnd).join(''), sprite });
    start = matchEnd;
  }
  if (plain) segments.push({ text: plain });
  return segments;
}

export function replaceEmojiText(element) {
  const document = element.ownerDocument;
  const walker = document.createTreeWalker(element, 4); // SHOW_TEXT
  const nodes = [];
  while (walker.nextNode()) nodes.push(walker.currentNode);
  for (const node of nodes) {
    const segments = emojiSegments(node.nodeValue);
    if (!segments.some(segment => segment.sprite !== undefined)) continue;
    const fragment = document.createDocumentFragment();
    for (const segment of segments) {
      if (segment.sprite === undefined) fragment.appendChild(document.createTextNode(segment.text));
      else {
        ensureSpriteRule(segment.sprite);
        const span = document.createElement('span');
        span.className = `emoji-sprite emoji-${segment.sprite}`;
        span.setAttribute('role', 'img');
        span.setAttribute('aria-label', segment.text);
        // Keep copyable/accessible text; CSS hides the unsupported native glyph.
        span.textContent = segment.text;
        fragment.appendChild(span);
      }
    }
    node.parentNode.replaceChild(fragment, node);
  }
}
