/**
 * [CN-FORK] v0.5.2.112 电视端「我在哪」指示（光圈）
 *
 * ────────────────────────────────────────────────────────────────────
 * 这一版是**回退**，先说清楚回退了什么、以及为什么
 * ────────────────────────────────────────────────────────────────────
 * 用户实机口径：「ps5真机上焦点鼠标移动的方式最好改回跟上游一样，现在这样很别扭」，
 * 追问后明确：「改回上游作者的处理方式一样，所有导航都出光圈，全部是鼠标跳到对应的位置。」
 *
 * 被回退的三轮改动（都在这个文件里，`.92` / `.110` / `.111`）：
 *   · `.92` 按几何位置**自己搬焦点**的空间导航（方向键 → pickNext 找最近控件 → focus）
 *   · `.110` 「确定键由我们自己点光圈那个元素」+ 捕获阶段把 click **改投**到光圈
 *   · `.111` 遥控器模式下**藏掉系统光标**
 *
 * 为什么是回退而不是继续修：**上游压根不碰键盘与指针**（本文件是 fork 独有的，上游没有对应物，
 * 上游的 Modal 也只有一颗关闭按钮），所以「改回上游」= 我们**什么都不接管**。
 * 三轮加厚的逻辑里，每一层都在补偿上一层带来的新问题（自己搬焦点 → 确定键点错位置 →
 * 于是抢确定键 → 于是出现两个「我在哪」→ 于是藏光标），越补越别扭。
 * 把它整层删掉之后，浏览器怎么走就是怎么走，那些补偿一个都不再需要。
 *
 * ────────────────────────────────────────────────────────────────────
 * 现在只做一件事：把光圈画在**指针当前压着的那个可操作元素**上
 * ────────────────────────────────────────────────────────────────────
 * 这正是用户那句「所有导航都出光圈，全部是鼠标跳到对应的位置」。两条路指向同一个
 * `.is-focus` 类（同一个 paint()，所以同一时刻只有一个元素带环）：
 *   · **指针路**：mousemove / pointermove 时用 `elementFromPoint` 找到指针下的元素，
 *     向上找到最近的可操作祖先，给它挂环。PS5 浏览器若用方向键驱动光标（发 trusted mousemove），
 *     这一步就自动跟着走 —— 不需要我们做任何按键处理。
 *   · **焦点路**：键盘 Tab 等原生焦点移动由 focusin 挂同一个环，键盘用户也看得见自己在哪。
 *
 * 三处**刻意不做**的：
 *   · **不动 `document.activeElement`**。只画环、不搬焦点，就不会出现「指针划过输入框、
 *     把正在打的字抢走焦点」这类事故。导航交给浏览器，我们只负责让用户看得见。
 *   · **不拦方向键**（不 preventDefault、不做任何几何跳转）。方向键留给浏览器原生行为。
 *   · **不留「首次落焦」**。没有指针压着任何控件时不画环 —— 光圈的定义就是「指针在哪」，
 *     凭空造一个位置只会又变成两个「我在哪」。
 * 因此 `.111` 的藏光标也一并撤掉：光圈与系统光标永远重合，藏它反而少了一个真实指示。
 *
 * ────────────────────────────────────────────────────────────────────
 * 保留的两件（与「光标」无关，且是本 fork 的真实缺陷修复）
 * ────────────────────────────────────────────────────────────────────
 *   ① 返回键派发 `ps5-back` 交外壳统一裁决（遥控器上没有浏览器返回按钮，弹窗关不掉、
 *      页面退不回去）。**屏上有浮层时把这次返回留给浮层**，见 onKeyDown。
 *   ② `?navdebug=1` 的只读排障面板：PS5 浏览器没有控制台，实机取证只能靠它。
 *
 * 兼容性：只用 ES 基础语法与 getBoundingClientRect / elementFromPoint，避开老内核
 * WebKit 不支持的 `:focus-visible`（光圈仍由这里手动挂 `.is-focus`，CSS 在 index.css）。
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

// 输入控件里的返回键要留给原生（退格删字），不能被我们拿去退页
function isTextEntry(el) {
  if (!el) return false;
  const tag = el.tagName;
  if (tag === 'TEXTAREA' || tag === 'SELECT') return true;
  if (tag !== 'INPUT') return false;
  const type = (el.getAttribute('type') || 'text').toLowerCase();
  return ['text', 'search', 'url', 'email', 'password', 'number', 'tel'].indexOf(type) !== -1;
}

/* 描述一个元素，供排障面板显示（老内核没有 console 可看）。
 * 名字优先取可见文字，**取不到就退回 aria-label** —— 站内大量控件是「只有图标」的按钮
 * （磁贴上四颗、各处 icon-btn），它们没有 textContent，只按文字描述的话面板上一排
 * `button.flex.items-center` 完全分不出谁是谁，排障时等于没有这一栏。 */
function describe(el) {
  if (!el) return '(none)';
  const tag = (el.tagName || '?').toLowerCase();
  const id = el.id ? '#' + el.id : '';
  const cls = (typeof el.className === 'string' && el.className)
    ? '.' + el.className.split(/\s+/).filter(Boolean).slice(0, 2).join('.')
    : '';
  const txt = (el.textContent || '').replace(/\s+/g, ' ').trim().slice(0, 16)
    || (el.getAttribute && (el.getAttribute('aria-label') || '').trim().slice(0, 16))
    || '';
  return tag + id + cls + (txt ? ' «' + txt + '»' : '');
}

export function enableSpatialNav() {
  let ringEl = null;      // 当前挂着 .is-focus 的那个元素（唯一，两条路共用 paint 才能保证）
  let rafId = 0;          // mousemove 很密，合并到一帧只做一次 DOM 查询
  let lastPoint = '';

  /* ── 排障面板：只在 ?navdebug=1 时建，平时零开销 ── */
  let hudEl = null;
  if (/[?&]navdebug=1/.test(window.location.search)) {
    hudEl = document.createElement('div');
    hudEl.setAttribute('data-navdebug', '1');
    hudEl.style.cssText =
      'position:fixed;top:8px;left:50%;transform:translateX(-50%);z-index:9999;' +
      'background:rgba(0,0,0,.82);color:#7dd3fc;border:1px solid #0ea5e9;border-radius:8px;' +
      'padding:6px 10px;font:12px/1.5 monospace;white-space:pre;pointer-events:none;max-width:92vw;';
    hudEl.textContent = 'navdebug: 等指针…';
    (document.body || document.documentElement).appendChild(hudEl);
  }
  const hud = (msg) => {
    if (!hudEl) return;
    hudEl.textContent = 'navdebug\n' + msg + '\nring : ' + describe(ringEl);
  };

  /* 唯一的光圈画手。为什么必须是唯一：focusin 和指针路都要动这个类，
   * 各自为政的话会出现「两个元素同时带环」（一个来自键盘、一个来自指针），
   * 那又回到了「两个我在哪」。 */
  function paint(el) {
    const next = el || null;
    if (ringEl === next) return;
    if (ringEl && ringEl.classList) ringEl.classList.remove('is-focus');
    ringEl = next;
    if (ringEl && ringEl.classList) ringEl.classList.add('is-focus');
  }

  /* 能不能作为光圈落点。太小的（老内核上 w/h 为 0 的隐藏控件）与不可见的都排除，
   * 否则指针划过一片空白时环会挂到某个看不见的节点上。 */
  function usable(el) {
    if (!el || el.nodeType !== 1) return false;
    if (el.disabled) return false;
    if (el.getAttribute && el.getAttribute('aria-hidden') === 'true') return false;
    const r = el.getBoundingClientRect();
    if (r.width < 6 || r.height < 6) return false;
    const cs = window.getComputedStyle(el);
    if (cs.visibility === 'hidden' || cs.display === 'none') return false;
    return true;
  }

  /* 从指针下的那个元素往上找最近的、可操作的祖先。
   * 必须是「往上找」而不是「只认命中元素」：磁贴上的图标、文字都盖在按钮上面，
   * 指针底下十有八九是个 <img> 或 <span>，而用户心里的「这个控件」是外面那颗按钮。 */
  function focusableFrom(node) {
    let el = node;
    while (el && el !== document.body && el !== document.documentElement) {
      if (el.matches && el.matches(FOCUSABLE) && usable(el)) return el;
      el = el.parentElement;
    }
    return null;
  }

  /* ── 指针路 ── */
  const onPointerMove = (e) => {
    if (typeof e.clientX !== 'number') return;
    const key = e.clientX + ',' + e.clientY;
    if (key === lastPoint) return;   // 同一位置的连续事件（指针事件与鼠标事件会成对来）只处理一次
    lastPoint = key;
    if (rafId) return;
    rafId = requestAnimationFrame(() => {
      rafId = 0;
      const under = document.elementFromPoint(e.clientX, e.clientY);
      const target = under ? focusableFrom(under) : null;
      paint(target);
      hud('pointer → ' + describe(target));
    });
  };

  /* ── 焦点路（键盘 Tab / 浏览器自己搬焦点）：
   * 与指针路共用 paint，所以「Tab 之后指针再一动」不会留下两个环。 */
  const onFocusIn = (e) => {
    if (usable(e.target)) paint(e.target);
  };
  const onFocusOut = (e) => {
    if (ringEl === e.target) paint(null);
  };

  /* 浮层开合时指针往往没动过，环会停在浮层底下的某个元素上 —— 视觉上像「环跑到弹窗后面去了」。
   * 浮层出现时先清掉环，等指针一动自然会重新落上（浮层里也一样）。 */
  const onDialogToggle = () => {
    if (document.querySelector('[role="dialog"]')) paint(null);
  };

  const onKeyDown = (e) => {
    if (e.ctrlKey || e.metaKey || e.altKey) return;
    if (e.key !== 'Escape' && e.key !== 'GoBack' && e.key !== 'Backspace') return;
    const active = document.activeElement;
    if (isTextEntry(active)) return; // 输入法里退格归原生
    /* [CN-FORK] v0.5.2.112 屏上有浮层时，返回键**只交给它**，不再往外抛 ps5-back。
     *
     * 修的是什么：本监听器挂在 window 的**捕获**阶段、且注册在 App 挂载时，而 Modal 自己的
     * Escape 监听挂在冒泡阶段 —— 捕获先跑。于是「在游戏详情里按返回键」会先抛一次
     * ps5-back（App 收到就把视图栈弹一格，从游戏页退回上一页），再让 Modal 关掉详情：
     * 用户看到的是「关掉弹窗的同时整页也被退掉了」，而且退回去那页是空的（视图已经切走），
     * 看起来像崩了。
     *
     * 判 role=dialog 而不是维护一份「谁开着」的名单：Modal 与多媒体预览两个组件用的是同一个
     * role（全站仅此两处），判它等于判「有没有东西该先处理这次返回」，不需要任何跨层通信。
     * dismissible=false 的浮层（安装/删除进行中）也因此不会被返回键绕过 —— 那正是它要的语义。 */
    if (document.querySelector('[role="dialog"]')) return;
    // 遥控器「返回」交给外壳统一裁决（先关弹窗，再退回上一页）
    window.dispatchEvent(new CustomEvent('ps5-back'));
  };

  document.addEventListener('focusin', onFocusIn);
  document.addEventListener('focusout', onFocusOut);
  window.addEventListener('keydown', onKeyDown, true);
  window.addEventListener('mousemove', onPointerMove, true);
  window.addEventListener('pointermove', onPointerMove, true);

  /* 浮层开合没有事件可用，用 MutationObserver 盯着 role=dialog 的出现/消失。
   * 只在**有浮层且环还挂着**时才做事，所以常态下这条观察者几乎是零开销。 */
  const mo = typeof MutationObserver === 'function'
    ? new MutationObserver(() => { if (ringEl || document.querySelector('[role="dialog"]')) onDialogToggle(); })
    : null;
  if (mo) mo.observe(document.documentElement, { childList: true, subtree: true });

  return () => {
    document.removeEventListener('focusin', onFocusIn);
    document.removeEventListener('focusout', onFocusOut);
    window.removeEventListener('keydown', onKeyDown, true);
    window.removeEventListener('mousemove', onPointerMove, true);
    window.removeEventListener('pointermove', onPointerMove, true);
    if (mo) mo.disconnect();
    if (rafId) cancelAnimationFrame(rafId);
    if (hudEl && hudEl.parentNode) hudEl.parentNode.removeChild(hudEl);
    paint(null);
  };
}
