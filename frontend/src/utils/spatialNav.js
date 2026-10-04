/**
 * [CN-FORK] v0.5.2.92 电视端空间导航（遥控器方向键）
 *
 * 为什么需要它：PS5 浏览器只有方向键 + OK + 返回，没有鼠标也没有触屏。此前全站
 * 没有任何按键逻辑（index.css 还写着 button:focus{outline:none}），焦点既看不见也
 * 搬不动，电视端实际上只能靠页面首屏那一个默认控件操作。
 *
 * 手法：不做逐组件的 roving tabindex（那要求每个列表都配合改造，上游新增页面必漏），
 * 而是按**几何位置**在「当前可见的可操作元素」里找方向上最近的一个来聚焦。这样任何
 * 页面只要用正常的 button/a/input 就能被遥控器走通；用 div onClick 的地方补
 * tabIndex/role 即可自动纳入。
 *
 * 兼容性：只用 ES 基础语法与 getBoundingClientRect，避开老内核 WebKit 不支持的
 * :focus-visible（焦点环改由这里手动挂 .is-focus 类）。
 */

const FOCUSABLE = [
  'a[href]',
  'button:not([disabled])',
  'input:not([type="hidden"]):not([disabled])',
  'select:not([disabled])',
  'textarea:not([disabled])',
  'label.btn',
  '[tabindex]:not([tabindex="-1"])',
].join(', ');

// 方向向量；主轴分量 * 自身 + 垂向分量取绝对值，得到「先同列、再就近」的代价函数
const DIRS = {
  ArrowUp: [0, -1],
  ArrowDown: [0, 1],
  ArrowLeft: [-1, 0],
  ArrowRight: [1, 0],
};

// 输入控件里方向键要留给原生行为（移动光标、翻 select），不能被我们抢走
function isTextEntry(el) {
  if (!el) return false;
  const tag = el.tagName;
  if (tag === 'TEXTAREA' || tag === 'SELECT') return true;
  if (tag !== 'INPUT') return false;
  const type = (el.getAttribute('type') || 'text').toLowerCase();
  return ['text', 'search', 'url', 'email', 'password', 'number', 'tel'].indexOf(type) !== -1;
}

function visibleRect(el) {
  const r = el.getBoundingClientRect();
  if (r.width < 6 || r.height < 6) return null;
  if (r.bottom <= 0 || r.top >= window.innerHeight) return null;
  if (r.right <= 0 || r.left >= window.innerWidth) return null;
  return r;
}

function collect() {
  const nodes = document.querySelectorAll(FOCUSABLE);
  const list = [];
  for (let i = 0; i < nodes.length; i++) {
    const el = nodes[i];
    const r = visibleRect(el);
    if (!r) continue;
    // 隐形原生 select（用 opacity-0 覆盖在自定义控件上的那种）不参与导航，交给它的 label
    if (window.getComputedStyle(el).opacity === '0') continue;
    list.push({ el: el, r: r, cx: r.left + r.width / 2, cy: r.top + r.height / 2 });
  }
  return list;
}

// 从起点出发，沿 (dx,dy) 方向找代价最小的下一个控件
function pickNext(from, list, dx, dy) {
  let best = null;
  let bestCost = Infinity;
  for (let i = 0; i < list.length; i++) {
    const c = list[i];
    if (c.el === from) continue;
    const ox = c.cx - from.cx;
    const oy = c.cy - from.cy;
    const along = ox * dx + oy * dy;
    if (along <= 2) continue; // 不在这个方向上
    const cross = Math.abs(ox * dy - oy * dx);
    const cost = along + cross * 3; // 垂距惩罚 3 倍：先走完同一列，再考虑旁边列
    if (cost < bestCost) {
      bestCost = cost;
      best = c;
    }
  }
  return best;
}

// 找到某个元素所属的滚动容器（本项目 <main> 自带 overflow-y-auto，窗口本身在电视端不滚）
function scrollParentOf(el) {
  let node = el;
  while (node && node !== document.body) {
    const style = window.getComputedStyle(node);
    const oy = style.overflowY;
    if ((oy === 'auto' || oy === 'scroll') && node.scrollHeight > node.clientHeight + 8) return node;
    node = node.parentElement;
  }
  return document.scrollingElement || document.documentElement;
}

function scrollByStep(scroller, dx, dy) {
  const stepY = Math.round(window.innerHeight * 0.7);
  const stepX = Math.round(window.innerWidth * 0.5);
  if (scroller === (document.scrollingElement || document.documentElement)) {
    window.scrollBy(dx * stepX, dy * stepY);
  } else {
    scroller.scrollTop += dy * stepY;
    scroller.scrollLeft += dx * stepX;
  }
}

function focusTarget(el) {
  try {
    el.focus();
  } catch (e) {
    /* 老内核在某些节点上会抛，忽略即可，下个按键再试 */
  }
  /* [CN-FORK] v0.5.2.98 挂焦点环不依赖 focusin 事件：老内核在页面刚加载、或某些节点上
     不一定派发 focusin，环不出现就等于遥控器「看不见自己在哪」。这里直接挂类，
     onFocusIn/onFocusOut 那套仍保留，两条路指向同一个 .is-focus。 */
  if (el && el.classList) {
    const prev = document.querySelector('.is-focus');
    if (prev && prev !== el) prev.classList.remove('is-focus');
    el.classList.add('is-focus');
  }
}

export function enableSpatialNav() {
  // 焦点环：老内核不认 :focus-visible，这里给获得焦点的元素手动挂类
  const onFocusIn = (e) => {
    const prev = document.querySelector('.is-focus');
    if (prev && prev !== e.target) prev.classList.remove('is-focus');
    if (e.target && e.target.classList) e.target.classList.add('is-focus');
  };
  const onFocusOut = (e) => {
    if (e.target && e.target.classList) e.target.classList.remove('is-focus');
  };

  const onKeyDown = (e) => {
    if (e.ctrlKey || e.metaKey || e.altKey) return;
    const dir = DIRS[e.key];
    /* [CN-FORK] v0.5.2.98 active 提到函数顶：原来只在 Enter 分支里声明，Escape 分支引用它
       会抛 ReferenceError，导致遥控器返回键整条路径失效。 */
    const active = document.activeElement;
    if (dir) {
      if (isTextEntry(active)) return; // 文本框里方向键归原生
      e.preventDefault();
      const list = collect();
      if (!list.length) return;
      /* [CN-FORK] v0.5.2.98 起点取「候选集合里的那一项」而不是裸元素：pickNext 读的是
         from.cx/from.cy（候选对象上的中心坐标），旧写法把元素直接传进去，坐标变成 undefined，
         代价函数全程 NaN → 任何方向都找不到下一个控件，电视端方向键整体失灵。
         另外 body/documentElement 的矩形也满足 visibleRect，旧写法会把它们当成当前焦点，
         于是连「首次落焦」分支都进不去。 */
      let from = null;
      if (active && active !== document.body && active !== document.documentElement) {
        for (let i = 0; i < list.length; i++) {
          if (list[i].el === active) { from = list[i]; break; }
        }
      }
      if (!from) {
        // 首次按下：把焦点落到当前屏上最靠上、最靠左的控件，让用户立刻看见自己在哪
        const sorted = list.slice().sort((a, b) => (a.r.top - b.r.top) || (a.r.left - b.r.left));
        focusTarget(sorted[0].el);
        return;
      }
      let next = pickNext(from, list, dir[0], dir[1]);
      if (next) {
        focusTarget(next.el);
        return;
      }
      // 这个方向已到屏内尽头：滚一屏，再在滚动后的画面里挑同方向的第一个控件
      const scroller = scrollParentOf(from.el);
      scrollByStep(scroller, dir[0], dir[1]);
      next = pickNext(from, collect(), dir[0], dir[1]);
      if (next) focusTarget(next.el);
      return;
    }
    if (e.key === 'Enter' || e.key === 'Spacebar' || e.keyCode === 13) {
      // 遥控器 OK 落在 <button>/<a> 上由原生激活；落在用 div 实现的可点击行上需要手动点一次
      if (active && active.tagName !== 'BUTTON' && active.tagName !== 'A' &&
          active.tagName !== 'INPUT' && active.tagName !== 'SELECT' && active.tagName !== 'TEXTAREA' &&
          (active.getAttribute('role') === 'button' || active.getAttribute('role') === 'link')) {
        e.preventDefault();
        active.click();
      }
      return;
    }
    if (e.key === 'Escape' || e.key === 'GoBack' || e.key === 'Backspace') {
      if (isTextEntry(active)) return; // 输入法里退格归原生
      // 遥控器「返回」交给外壳统一裁决（先关弹窗，再退回上一页）
      window.dispatchEvent(new CustomEvent('ps5-back'));
    }
  };

  document.addEventListener('focusin', onFocusIn);
  document.addEventListener('focusout', onFocusOut);
  window.addEventListener('keydown', onKeyDown, true);

  return () => {
    document.removeEventListener('focusin', onFocusIn);
    document.removeEventListener('focusout', onFocusOut);
    window.removeEventListener('keydown', onKeyDown, true);
  };
}
