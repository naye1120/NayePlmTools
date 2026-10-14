/* eslint-disable no-undef */
/**
 * Polyfills for older WebKit / Safari 11/12 (PS5 browser) runtime compatibility.
 */

// globalThis
if (typeof globalThis === 'undefined') {
  var getGlobal = function() {
    if (typeof self !== 'undefined') return self;
    if (typeof window !== 'undefined') return window;
    if (typeof global !== 'undefined') return global;
    return Function('return this')();
  };
  var g = getGlobal();
  if (g) {
    try {
      Object.defineProperty(g, 'globalThis', {
        value: g,
        writable: true,
        configurable: true,
      });
    } catch {
      g.globalThis = g;
    }
  }
}

// Node.js 'global' in browser
if (typeof window !== 'undefined' && typeof window.global === 'undefined') {
  window.global = window;
}

// Array.prototype.at
if (!Array.prototype.at) {
  Array.prototype.at = function(n) {
    var k = Math.trunc(n) || 0;
    if (k < 0) k += this.length;
    if (k < 0 || k >= this.length) return undefined;
    return this[k];
  };
}

// String.prototype.at
if (!String.prototype.at) {
  String.prototype.at = function(n) {
    var k = Math.trunc(n) || 0;
    if (k < 0) k += this.length;
    if (k < 0 || k >= this.length) return undefined;
    return this.charAt(k);
  };
}

// TypedArray.prototype.at
var TypedArrayProto = typeof Int8Array !== 'undefined' ? Object.getPrototypeOf(Int8Array.prototype) : null;
if (TypedArrayProto && !TypedArrayProto.at) {
  TypedArrayProto.at = Array.prototype.at;
}

// Array.prototype.flat
if (!Array.prototype.flat) {
  Array.prototype.flat = function(depth) {
    var d = depth === undefined ? 1 : (depth === Infinity || depth === 1/0 ? 999999 : (Math.floor(depth) || 0));
    var result = [];
    (function flatten(arr, curDepth) {
      for (var i = 0; i < arr.length; i++) {
        if (i in arr) {
          var val = arr[i];
          if (Array.isArray(val) && curDepth > 0) {
            flatten(val, curDepth - 1);
          } else {
            result.push(val);
          }
        }
      }
    })(this, d);
    return result;
  };
}

// Array.prototype.flatMap
if (!Array.prototype.flatMap) {
  Array.prototype.flatMap = function(callback, thisArg) {
    return this.map(callback, thisArg).flat();
  };
}

// Object.fromEntries
if (!Object.fromEntries) {
  Object.fromEntries = function(entries) {
    if (!entries) return {};
    var obj = {};
    for (var entry of entries) {
      if (entry) {
        obj[entry[0]] = entry[1];
      }
    }
    return obj;
  };
}

// String.prototype.replaceAll
if (!String.prototype.replaceAll) {
  String.prototype.replaceAll = function(searchValue, replaceValue) {
    if (searchValue instanceof RegExp) {
      if (!searchValue.global) {
        throw new TypeError('String.prototype.replaceAll called with a non-global RegExp');
      }
      return this.replace(searchValue, replaceValue);
    }
    return this.split(searchValue).join(replaceValue);
  };
}

// Promise.allSettled
if (!Promise.allSettled) {
  Promise.allSettled = function(promises) {
    return Promise.all(
      Array.from(promises).map(function(p) {
        return Promise.resolve(p).then(
          function(value) { return { status: 'fulfilled', value: value }; },
          function(reason) { return { status: 'rejected', reason: reason }; }
        );
      })
    );
  };
}

// queueMicrotask
if (typeof queueMicrotask !== 'function') {
  var qm = function(fn) {
    Promise.resolve().then(fn).catch(function(err) {
      setTimeout(function() { throw err; }, 0);
    });
  };
  if (typeof window !== 'undefined') window.queueMicrotask = qm;
  if (typeof self !== 'undefined') self.queueMicrotask = qm;
  if (typeof globalThis !== 'undefined') globalThis.queueMicrotask = qm;
  if (typeof global !== 'undefined') global.queueMicrotask = qm;
}

// Object.hasOwn
if (!Object.hasOwn) {
  Object.hasOwn = function(obj, prop) {
    return Object.prototype.hasOwnProperty.call(obj, prop);
  };
}
