/**
 * Version comparison and slug generation utilities matching C backend (slot.c).
 */

export function slug(input) {
  if (!input || typeof input !== 'string') return 'payload';
  let s = '';
  let lastDash = true;

  for (let i = 0; i < input.length; i++) {
    const ch = input[i];
    const code = ch.charCodeAt(0);
    const isAlnum =
      (code >= 48 && code <= 57) || // 0-9
      (code >= 65 && code <= 90) || // A-Z
      (code >= 97 && code <= 122);  // a-z

    if (isAlnum) {
      s += ch.toLowerCase();
      lastDash = false;
    } else if ((ch === '.' || ch === '_') && s.length > 0 && s[s.length - 1] !== '.') {
      s += ch;
      lastDash = false;
    } else {
      if (!lastDash) {
        s += '-';
        lastDash = true;
      }
    }
  }

  // Strip trailing dashes, dots, or underscores
  s = s.replace(/[-._]+$/, '');
  return s.length > 0 ? s : 'payload';
}

/**
 * Tokenizes a version string into runs of digits and runs of letters.
 * Any character that is not alphanumeric acts as a token separator.
 */
function tokenizeVersion(ver) {
  if (!ver || typeof ver !== 'string') return [];
  let s = ver.trim();
  if (s.startsWith('v') || s.startsWith('V')) {
    s = s.slice(1).trim();
  }
  const tokens = [];
  let i = 0;
  while (i < s.length) {
    const ch = s[i];
    const isDigit = ch >= '0' && ch <= '9';
    const isLetter = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');

    if (isDigit) {
      let start = i;
      while (i < s.length && s[i] >= '0' && s[i] <= '9') {
        i++;
      }
      tokens.push({ type: 'digit', val: s.slice(start, i) });
    } else if (isLetter) {
      let start = i;
      while (
        i < s.length &&
        ((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z'))
      ) {
        i++;
      }
      tokens.push({ type: 'letter', val: s.slice(start, i) });
    } else {
      // Separator
      i++;
    }
  }
  return tokens;
}

/**
 * Compares two version strings according to redesign spec §4:
 * 1. Trim, drop leading v/V.
 * 2. Tokenize into digit runs and letter runs (non-alnum separate tokens).
 * 3. Digits compare numerically (without leading zeros). Letters compare case-insensitively.
 *    Digit > letter at the same position.
 * 4. When one version runs out first: a next token of letters on the other side
 *    means pre-release, so lower (1.7 > 1.7beta3). Digits mean higher (1.0.1 > 1.0).
 * 5. Tie-breaker for different strings that compare equal: last_update.
 *
 * Returns:
 *   1 if a > b
 *  -1 if a < b
 *   0 if equal
 */
export function compareVersions(a, b, lastUpdateA = null, lastUpdateB = null) {
  if (!a && !b) return tieBreak(lastUpdateA, lastUpdateB);
  if (!a) return -1;
  if (!b) return 1;

  const tokensA = tokenizeVersion(a);
  const tokensB = tokenizeVersion(b);

  if (tokensA.length === 0 && tokensB.length === 0) {
    return tieBreak(lastUpdateA, lastUpdateB);
  }
  if (tokensA.length === 0) return -1;
  if (tokensB.length === 0) return 1;

  const maxLen = Math.max(tokensA.length, tokensB.length);
  for (let i = 0; i < maxLen; i++) {
    const tA = tokensA[i];
    const tB = tokensB[i];

    if (!tA) {
      // a ran out first
      // If b's next token is letters, b is a pre-release -> a > b
      if (tB.type === 'letter') return 1;
      // If b's next token is digits, b is a sub-version -> a < b
      return -1;
    }

    if (!tB) {
      // b ran out first
      // If a's next token is letters, a is a pre-release -> a < b
      if (tA.type === 'letter') return -1;
      // If a's next token is digits, a is a sub-version -> a > b
      return 1;
    }

    // Both tokens exist at position i
    if (tA.type === 'digit' && tB.type === 'letter') return 1;
    if (tA.type === 'letter' && tB.type === 'digit') return -1;

    if (tA.type === 'digit' && tB.type === 'digit') {
      // Strip leading zeros
      const strA = tA.val.replace(/^0+(?=\d)/, '');
      const strB = tB.val.replace(/^0+(?=\d)/, '');

      if (strA.length !== strB.length) {
        return strA.length > strB.length ? 1 : -1;
      }
      if (strA !== strB) {
        return strA > strB ? 1 : -1;
      }
    } else {
      // Both are letters: compare case-insensitively
      const lowerA = tA.val.toLowerCase();
      const lowerB = tB.val.toLowerCase();
      if (lowerA !== lowerB) {
        return lowerA > lowerB ? 1 : -1;
      }
    }
  }

  // Token sequences are identical: apply tie-breaker if dates differ
  return tieBreak(lastUpdateA, lastUpdateB);
}

function tieBreak(dateA, dateB) {
  if (!dateA || !dateB) return 0;
  if (dateA === dateB) return 0;

  // Compare timestamps if parseable, or lexicographically (ISO dates)
  const timeA = Date.parse(dateA);
  const timeB = Date.parse(dateB);
  if (!Number.isNaN(timeA) && !Number.isNaN(timeB)) {
    if (timeA > timeB) return 1;
    if (timeA < timeB) return -1;
    return 0;
  }

  return dateA > dateB ? 1 : (dateA < dateB ? -1 : 0);
}
