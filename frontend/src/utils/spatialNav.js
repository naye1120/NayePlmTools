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
 *
 * ────────────────────────────────────────────────────────────────────
 * [CN-FORK] v0.5.2.110 修「光圈跟着方向键走，但按 OK 生效的是鼠标位置」
 * ────────────────────────────────────────────────────────────────────
 * 用户实机口径：「ps5端按上下按钮的时候 会有一个彩色光圈，但是点击确定的时候依然是
 * 鼠标焦点生效，所以，这个功能可以考虑，鼠标也要跟着移动。」
 *
 * **为什么不能真的去挪鼠标**：网页里没有任何 API 能移动系统/浏览器的指针（Pointer Lock
 * 只能锁不能移，合成 mousemove 也移动不了真光标）。所以「让鼠标跟着走」这条路走不通。
 * 但用户真正要的结果是**「按 OK 一定落在光圈那个元素上」**，而这个可以做到 —— 把 OK
 * 从「浏览器在指针处点一下」改成「我们自己点光圈那个元素」，指针在哪就无关紧要了。
 *
 * 三条规则（都在**捕获阶段**拦 click，且只认真实事件 `e.isTrusted`）：
 *   ① 我们刚用 OK 亲自激活过 → 800ms 内那一发 click 一律吞掉。因为 OK 有可能是
 *      「Enter keydown + 浏览器在指针位置补一个合成 click」双份投递，不吞就双触发。
 *   ② 指针用户放行：真·指针动过（trusted mousemove）之后，直到下一次按方向键为止，
 *      click 原样放行 —— 插了 USB 鼠标的人照旧用鼠标。
 *   ③ 其余 click = 遥控器/键盘的「确定」在指针位置点了一下 → 改投到光圈元素上。
 *
 * **方向键/OK 自身带出来的光标事件要滤掉**：如果 PS5 浏览器在方向键或 OK 时顺带挪一下自己的
 * 光标（会发 trusted mousemove），那它就会把 ② 误打开、规则 ③ 失效 —— 而且是「看着屏幕想两秒
 * 再按确定」这种最普通的节奏就会中招（那时早已超出任何时间窗）。所以「是不是指针用户」不能只
 * 看「有没有 mousemove」，要**看指针有没有真的被挪走**。两条一起用：
 *   · **时间窗**：按过方向键之后的 400ms 内收到的 mousemove 一概不算。
 *   · **位移判据**：方向键后的**第一发** mousemove 只用来立基准（记下坐标），从第二发起必须
 *     相对基准位移超过 12px 才算「用户在动鼠标」。方向键/OK 顺手带出来的光标事件是原地或
 *     微动，永远过不了这一关；真鼠标是持续事件流，一推就过。
 * 这样即使浏览器在 OK 时发一发 mousemove，规则 ③ 依然稳定生效；而插了 USB 鼠标的人照旧用鼠标。
 *
 * **排障开关**：URL 带 `?navdebug=1` 时右上角挂一个只读小面板，实时显示最近一次
 * keydown / click 的处理结果（放行、吞掉、改投、以及改投到了谁）。实机复现不了、
 * 又没有控制台，这个小面板就是唯一的取证手段。 */
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

/* 描述一个元素，供排障面板显示（老内核没有 console 可看） */
function describe(el) {
  if (!el) return '(none)';
  const tag = (el.tagName || '?').toLowerCase();
  const id = el.id ? '#' + el.id : '';
  const cls = (typeof el.className === 'string' && el.className)
    ? '.' + el.className.split(/\s+/).filter(Boolean).slice(0, 2).join('.')
    : '';
  const txt = (el.textContent || '').replace(/\s+/g, ' ').trim().slice(0, 16);
  return tag + id + cls + (txt ? ' «' + txt + '»' : '');
}

export function enableSpatialNav() {
  /* ── v0.5.2.110 遥控器「确定」的状态 ── */
  let navTarget = null;      // 光圈所在元素（不信 document.activeElement：老内核上它未必跟得上）
  let pointerMode = false;   // 真·指针动过 → 直到下次按方向键为止，click 全部放行
  let arrowGraceUntil = 0;   // 方向键后的静默窗：这段里的 mousemove 视为方向键自己的光标抖动
  let pendingAnchor = false; // 方向键后还没收到第一发 mousemove（那一发只用来立基准）
  let pointerAnchor = null;  // 位移判据的基准坐标 {x,y}；null = 还没有基准
  let okArmedAt = 0;         // 刚由 OK 键亲自激活过，等吞掉随后那一发合成 click

  const ARROW_GRACE_MS = 400;
  const POINTER_MOVE_PX = 12; // 位移超过这个距离才算「用户真的在动鼠标」（不是光标抖动）
  const OK_SWALLOW_MS = 800;

  /* 排障面板：只在 ?navdebug=1 时建，平时零开销 */
  let hudEl = null;
  if (/[?&]navdebug=1/.test(window.location.search)) {
    hudEl = document.createElement('div');
    hudEl.setAttribute('data-navdebug', '1');
    hudEl.style.cssText =
      'position:fixed;top:8px;left:50%;transform:translateX(-50%);z-index:9999;' +
      'background:rgba(0,0,0,.82);color:#7dd3fc;border:1px solid #0ea5e9;border-radius:8px;' +
      'padding:6px 10px;font:12px/1.5 monospace;white-space:pre;pointer-events:none;max-width:92vw;';
    hudEl.textContent = 'navdebug: 等按键…';
    (document.body || document.documentElement).appendChild(hudEl);
  }
  const hud = (msg) => {
    if (!hudEl) return;
    hudEl.textContent =
      'navdebug\n' + msg +
      '\nring : ' + describe(navTarget) +
      '\nmode : ' + (pointerMode ? 'pointer' : 'remote');
  };

  /* 焦点环：老内核不认 :focus-visible，这里给获得焦点的元素手动挂类 */
  const onFocusIn = (e) => {
    const prev = document.querySelector('.is-focus');
    if (prev && prev !== e.target) prev.classList.remove('is-focus');
    if (e.target && e.target.classList) e.target.classList.add('is-focus');
  };
  const onFocusOut = (e) => {
    if (e.target && e.target.classList) e.target.classList.remove('is-focus');
  };

  function focusTarget(el) {
    try {
      el.focus();
    } catch {
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
    /* [CN-FORK] v0.5.2.110 光圈是权威：确定键一律以这个元素为准，不再看指针。 */
    navTarget = el || null;
  }

  /* [CN-FORK] v0.5.2.110 由我们自己激活光圈元素。
   * el.click() 派发的是 untrusted click —— 下面 onClickCapture 开头就把 untrusted 放走，
   * 所以这里不会被自己拦下来，React 的 onClick 正常触发。 */
  function activate(el) {
    if (!el) return;
    try {
      el.click();
    } catch {
      /* 老内核偶发抛错，忽略 */
    }
  }

  /* 真·指针移动：切换回「鼠标用户」模式。
   * 方向键/OK 顺带带出来的光标事件必须滤掉，否则规则 ③ 会被误关掉 —— 见文件头注释。 */
  const onPointerMove = (e) => {
    if (Date.now() < arrowGraceUntil) return;                 // ① 方向键后 400ms 内，一律不算
    const x = typeof e.clientX === 'number' ? e.clientX : null;
    const y = typeof e.clientY === 'number' ? e.clientY : null;
    if (pendingAnchor) {                                      // ② 方向键后的第一发：只立基准
      pendingAnchor = false;
      pointerAnchor = { x: x, y: y };
      return;
    }
    if (pointerAnchor && x !== null && y !== null) {           // ③ 没挪走多少 → 还是光标抖动
      const dx = x - pointerAnchor.x;
      const dy = y - pointerAnchor.y;
      if (dx * dx + dy * dy < POINTER_MOVE_PX * POINTER_MOVE_PX) return;
    }
    if (!pointerMode) hud('pointer moved → 鼠标模式（click 放行）');
    pointerMode = true;
  };

  /* [CN-FORK] v0.5.2.110 捕获阶段拦 click：让「确定」落在光圈上，而不是指针位置。 */
  const onClickCapture = (e) => {
    if (!e.isTrusted) return; // 我们自己 el.click() 发的，放走
    if (navTarget && !document.contains(navTarget)) navTarget = null;
    if (!navTarget) return;

    const now = Date.now();
    // ① 刚由 OK 键亲自激活过：这一发很可能是浏览器在指针位置补的，吞掉（防双触发/防点错）
    if (okArmedAt && now - okArmedAt < OK_SWALLOW_MS && !pointerMode) {
      okArmedAt = 0;
      e.preventDefault();
      e.stopPropagation();
      hud('click 吞掉（OK 已激活过）');
      return;
    }
    // ② 指针用户：完全不干预
    if (pointerMode) return;
    // ③ 这一发落在光圈自己身上/里面，本来就是对的，放行
    if (navTarget === e.target || navTarget.contains(e.target)) return;
    // ④ 其余 = 遥控器/键盘的「确定」点在了指针位置 → 改投到光圈
    e.preventDefault();
    e.stopPropagation();
    hud('click 改投 → ' + describe(e.target));
    activate(navTarget);
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
      /* [CN-FORK] v0.5.2.110 一按方向键就切回「遥控器模式」，并给方向键/OK 带出来的
         光标事件设两道闸：时间窗（400ms）+ 位移基准（第一发只立基准，之后要挪够 12px）。 */
      pointerMode = false;
      arrowGraceUntil = Date.now() + ARROW_GRACE_MS;
      pendingAnchor = true;
      pointerAnchor = null;
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
      /* [CN-FORK] v0.5.2.110 光圈所在元素优先当起点：老内核上 document.activeElement
         未必真的落在我们挂环的那个元素上，那样「从当前位置走一格」就会从别处算起。 */
      if (!from && navTarget) {
        for (let i = 0; i < list.length; i++) {
          if (list[i].el === navTarget) { from = list[i]; break; }
        }
      }
      if (!from) {
        // 首次按下：把焦点落到当前屏上最靠上、最靠左的控件，让用户立刻看见自己在哪
        const sorted = list.slice().sort((a, b) => (a.r.top - b.r.top) || (a.r.left - b.r.left));
        focusTarget(sorted[0].el);
        hud('首次落焦 → ' + describe(sorted[0].el));
        return;
      }
      let next = pickNext(from, list, dir[0], dir[1]);
      if (next) {
        focusTarget(next.el);
        hud(e.key + ' → ' + describe(next.el));
        return;
      }
      // 这个方向已到屏内尽头：滚一屏，再在滚动后的画面里挑同方向的第一个控件
      const scroller = scrollParentOf(from.el);
      scrollByStep(scroller, dir[0], dir[1]);
      next = pickNext(from, collect(), dir[0], dir[1]);
      if (next) focusTarget(next.el);
      hud(e.key + ' 滚屏 → ' + (next ? describe(next.el) : '(无候选)'));
      return;
    }
    if (e.key === 'Enter' || e.key === 'Spacebar' || e.key === ' ' || e.keyCode === 13) {
      /* [CN-FORK] v0.5.2.110 「确定」不再依赖浏览器的原生激活 —— 实机上那一步落在**指针位置**，
         光圈在哪儿都没用。改成：preventDefault 掉原生动作，自己点光圈那个元素。
         这样无论 OK 是「Enter keydown」「指针处 click」还是两者都发，结果都一致。
         输入控件里的回车仍然归原生（提交 / 换行），不抢。 */
      /* 正在输入框里打字时，回车归输入框 —— 这一条必须看「真正被聚焦的那个」，
         不能看 navTarget：用户可能用鼠标点进了改名框，而光圈还停在某个按钮上。 */
      if (isTextEntry(active)) return;
      const target = navTarget && document.contains(navTarget)
        ? navTarget
        : (active && active !== document.body && active !== document.documentElement ? active : null);
      if (!target || isTextEntry(target)) return;
      e.preventDefault();
      okArmedAt = Date.now();
      hud('OK → ' + describe(target));
      activate(target);
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
  document.addEventListener('click', onClickCapture, true);
  window.addEventListener('keydown', onKeyDown, true);
  window.addEventListener('mousemove', onPointerMove, true);
  window.addEventListener('pointermove', onPointerMove, true);

  return () => {
    document.removeEventListener('focusin', onFocusIn);
    document.removeEventListener('focusout', onFocusOut);
    document.removeEventListener('click', onClickCapture, true);
    window.removeEventListener('keydown', onKeyDown, true);
    window.removeEventListener('mousemove', onPointerMove, true);
    window.removeEventListener('pointermove', onPointerMove, true);
    if (hudEl && hudEl.parentNode) hudEl.parentNode.removeChild(hudEl);
    navTarget = null;
  };
}
