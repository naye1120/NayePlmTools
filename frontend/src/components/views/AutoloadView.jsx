import React, { useState, useEffect } from 'react'
import { RefreshCw, ArrowLeft, ArrowRight, Activity, Zap, ChevronUp, ChevronDown, Trash2, CheckCircle2 } from 'lucide-react'
import { cn, isPS5, isSystemPayload } from '../../utils/helpers'
import { useTranslation } from 'react-i18next'
import PayloadName from '../ui/PayloadName'
import Modal from '../ui/Modal'
import PageHead from '../ui/PageHead' /* [CN-FORK] v0.5.2.94 页头粘顶 + 空态归一 */
import EmptyState from '../ui/EmptyState'

const AutoloadView = ({ payloads, config, onSaveConfig, onToast, onRedirect }) => {
  const { t } = useTranslation()
  const [subView, setSubView] = useState('list')
  const [enabled, setEnabled] = useState(false)
  const [autoloadList, setAutoloadList] = useState([])
  const [showDelayModal, setShowDelayModal] = useState(false)
  const [customDelay, setCustomDelay] = useState('')
  const [saved, setSaved] = useState(false)
  const [saving, setSaving] = useState(false)
  const [isInitialized, setIsInitialized] = useState(false)
  const lastSyncedRef = React.useRef('')

  // Load initial config
  useEffect(() => {
    if (config) {
      const en = config.AUTOLOAD_ENABLED === true || config.AUTOLOAD_ENABLED === "true"
      const listStr = config.AUTOLOAD_LIST || ''
      setEnabled(en)
      setAutoloadList(listStr.split(',').filter(x => x))
      lastSyncedRef.current = `${en}:${listStr}`
      setIsInitialized(true)
    }
  }, [config])

  // Debounced Auto-Save
  useEffect(() => {
    if (!isInitialized) return

    const currentState = `${enabled}:${autoloadList.join(',')}`
    if (currentState === lastSyncedRef.current) return

    const timer = setTimeout(async () => {
      const shouldEnable = enabled
      const finalList = autoloadList.map(p => p === 'DELAY' ? '!1000' : p)
      const finalStr = finalList.join(',')

      setSaving(true)
      const success = await onSaveConfig({
        AUTOLOAD_ENABLED: shouldEnable,
        AUTOLOAD_LIST: finalStr
      })

      if (success) {
        lastSyncedRef.current = `${shouldEnable}:${finalStr}`
        setSaved(true)
        setTimeout(() => setSaved(false), 2000)
      }
      setSaving(false)
    }, 1500)

    return () => clearTimeout(timer)
  }, [autoloadList, enabled, isInitialized, onSaveConfig])

  const internalPayloads = payloads.filter(p => !p.includes('/mnt/usb') && !isSystemPayload(p)).map(p => p.split('/').pop())
  const availablePayloads = internalPayloads.filter(p => !autoloadList.includes(p))

  const handleToggle = (val) => {
    setEnabled(val)
  }

  const addPayload = (p) => {
    const isKstuff = p.toLowerCase().includes('kstuff');
    if (isKstuff) {
      const existing = autoloadList.find(x => x.toLowerCase().includes('kstuff'));
      if (existing) {
        onToast(t("autoload.conflict_kstuff", "Conflict: Multiple KStuff payloads detected."), 'error');
        return;
      }
    }
    setAutoloadList([...autoloadList, p]);
    setSubView('list')
  }

  const addDelay = (ms) => {
    setAutoloadList([...autoloadList, `!${ms}`])
    setShowDelayModal(false)
    setSubView('list')
  }

  const moveUp = (index) => {
    if (index === 0) return
    const newList = [...autoloadList]
      ;[newList[index - 1], newList[index]] = [newList[index], newList[index - 1]]
    setAutoloadList(newList)
  }

  const moveDown = (index) => {
    if (index === autoloadList.length - 1) return
    const newList = [...autoloadList]
      ;[newList[index + 1], newList[index]] = [newList[index], newList[index + 1]]
    setAutoloadList(newList)
  }

  const renderAvailable = () => (
    <div className="space-y-8 flex flex-col h-full min-h-0">
      <div className="flex items-center justify-between shrink-0">
        <div className="flex items-center space-x-4">
          {subView === 'add' && (
            <button onClick={() => setSubView('list')} className="p-2 bg-white/5 rounded-xl border border-white/10 lg:hidden">
              <ArrowLeft className="w-5 h-5" />
            </button>
          )}
          <h3 className="label-caps !text-white !opacity-100 text-xl tracking-widest">{t("autoload.available_title", "Available Payloads")}</h3>
        </div>
      </div>
      <div className="flex-1 overflow-y-auto pr-2 custom-scrollbar min-h-0 pb-6">
        <div className="grid grid-cols-1 gap-4">
          {availablePayloads.map(p => {
            const isKstuff = p.toLowerCase().includes('kstuff');
            const hasKstuff = autoloadList.some(x => x.toLowerCase().includes('kstuff'));
            const isBlocked = isKstuff && hasKstuff;

            return (
              <button
                key={p}
                onClick={() => !isBlocked && addPayload(p)}
                disabled={isBlocked}
                className={cn(
                  "flex items-start justify-between p-6 glass-card rounded-2xl border-white/20 transition-all text-left",
                  isBlocked ? "opacity-40 cursor-not-allowed" : "bg-white/[0.03] hover:border-ps-blue group"
                )}
              >
                <PayloadName path={p} className={cn(isBlocked ? "text-zinc-500" : "text-white")} stacked />
                <ArrowRight className={cn("w-6 h-6 transition-all shrink-0 mt-1", isBlocked ? "text-zinc-800" : "text-zinc-500 group-hover:text-ps-blue group-hover:translate-x-2")} />
              </button>
            )
          })}
          <div className="pt-4 border-t border-white/10 mt-4">
            <button
              onClick={() => setShowDelayModal(true)}
              className="w-full flex items-center justify-between p-6 bg-white/[0.03] rounded-2xl border border-dashed border-white/20 hover:border-ps-blue group transition-all"
            >
              <div className="flex items-center space-x-4">
                <Zap className="w-6 h-6 text-ps-blue" />
                <span className="font-bold text-white uppercase tracking-tight text-xl">{t("autoload.add_delay_btn", "Add Delay")}</span>
              </div>
              <ArrowRight className="w-6 h-6 text-zinc-500 group-hover:text-ps-blue group-hover:translate-x-2 transition-all" />
            </button>
          </div>
          <div className="pt-8 border-t border-white/5 mt-8 text-center space-y-4">
            <p className="text-zinc-500 text-sm font-bold uppercase tracking-widest opacity-60">{t("autoload.missing_payload", "Missing a payload?")}</p>
            <button
              onClick={() => onRedirect('storage', 'usb-storage')}
              className="group flex flex-col items-center mx-auto space-y-3"
            >
              <div className="flex items-center space-x-3 text-ps-blue group-hover:text-white transition-colors">
                <span className="font-black italic text-lg uppercase tracking-tight">{t("autoload.move_usb_btn", "Move from USB to Internal")}</span>
              </div>
              <p className="text-xs text-zinc-600 max-w-[200px] leading-relaxed">{t("autoload.move_usb_desc", "Required for payloads you want to use in the Autoload sequence.")}</p>
            </button>
          </div>
        </div>
      </div>
    </div>
  )

  /* [CN-FORK] v0.5.2.99 页面级大标题（<PageHead>）上移到整页最顶，与其他菜单页同一档；
   * 序列这一列随之降为「板块」：原来压在列首的 24px 页头改成与左列「可用的 Payload」
   * 同款的板块小标题，保存状态与「停用自启」交给页头，列里只剩列表本体。 */
  const renderSequence = () => (
    <div className="space-y-4 flex flex-col h-full min-h-0">
      <h3 className="shrink-0 label-caps !text-white !opacity-100 text-xl tracking-widest">
        {t("autoload.sequence_title_1", "Autoload")} <span className="text-ps-blue">{t("autoload.sequence_title_2", "Sequence")}</span>
      </h3>

      <div className="glass-panel p-4 rounded-ps-2xl border-white/10 flex-1 overflow-hidden flex flex-col min-h-0">
        <div className="flex-1 overflow-y-auto custom-scrollbar space-y-4 pr-2 mb-2 pb-6">
          {autoloadList.map((p, i) => (
            <div key={`${p}-${i}`} className="relative row-link flex items-center justify-between p-4 bg-white/5 rounded-xl border border-white/10">
              {/* [CN-FORK] v0.5.2.99 序号从「绝对定位压在行左上角」改成正常流里的前导列。
                  旧写法有两个问题：一是它的底 bg-dark 在项目里根本没有定义（Tailwind 生成不出这条
                  规则 → 透明），数字是悬空压在 payload 名字上的；二是它锚在行的 border-box 角上，
                  名字容器只给了 pl-2，450px 宽下两者实测重叠 16×14px，第一个字被序号盖住。
                  现在序号占固定 1.5rem 前导列，名字跟着往后排，谁也不叠谁。 */}
              <div className="flex items-center min-w-0">
                <span className="w-6 flex-none text-center text-[12px] font-black text-zinc-500 tabular-nums">{i + 1}</span>
                <div className="min-w-0 ml-1">
                  <PayloadName path={p} className="text-white" stacked />
                </div>
              </div>
              {/* [CN-FORK] v0.5.2.94 三个 32px 小方钮换成共用 .icon-btn（与 PC 44px / 电视 68px 同一档命中区）；
                  原来 hover 才变实心橙，电视端没有 hover，看着像三块不能按的灰片。 */}
              <div className="flex items-center">
                <button onClick={() => moveUp(i)} disabled={i === 0} title={t("autoload.move_up", "上移")} className="icon-btn">
                  <ChevronUp />
                </button>
                <button onClick={() => moveDown(i)} disabled={i === autoloadList.length - 1} title={t("autoload.move_down", "下移")} className="icon-btn">
                  <ChevronDown />
                </button>
                <button onClick={() => setAutoloadList(autoloadList.filter((_, idx) => idx !== i))} title={t("autoload.remove_item", "移出序列")} className="icon-btn is-danger">
                  <Trash2 />
                </button>
              </div>
            </div>
          ))}
          {autoloadList.length === 0 && (
            <EmptyState icon={Activity} title={t("autoload.sequence_empty", "Sequence Empty")} dashed={false} className="py-16" />
          )}

          <div className={cn("pt-4 mt-2", isPS5 ? "hidden" : "lg:hidden")}>
            <button
              onClick={() => setSubView('add')}
              className="w-full flex items-center justify-center space-x-3 p-4 bg-ps-blue/10 hover:bg-ps-blue text-ps-blue hover:text-white rounded-2xl border border-dashed border-ps-blue/30 hover:border-ps-blue transition-all group shadow-lg"
            >
              <Activity className="w-5 h-5" />
              <span className="font-black italic text-base uppercase tracking-tighter">{t("autoload.add_item_btn", "Add Item to Sequence")}</span>
            </button>
          </div>
        </div>
      </div>
    </div>
  )

  if (!enabled) {
    return (
      <div className="flex-1 flex flex-col items-center justify-center space-y-8 text-center p-6 md:p-12">
        <div className="relative h-24 w-24 md:h-32 md:w-32 mx-auto">
          <div className="absolute inset-0 bg-ps-blue/20 rounded-full" />
          <div className="relative flex items-center justify-center h-full w-full bg-black/40 border border-white/10 rounded-3xl md:rounded-[2.5rem] shadow-2xl">
            <RefreshCw className="w-10 h-10 md:w-16 md:h-16 text-ps-blue" />
          </div>
        </div>
        <div className="space-y-3 md:space-y-4 px-4 max-w-2xl">
          <h2 className="text-3xl md:text-5xl font-black text-white uppercase italic tracking-tighter">
            {t("autoload.sequence_title_1", "Autoload")} <span className="text-ps-blue">{t("autoload.sequence_title_2", "Sequence")}</span>
          </h2>
          <p className="text-md md:text-xl text-zinc-400 font-medium leading-relaxed">
            {t("autoload.enable_desc", "Chain multiple payloads to be executed automatically every time Payload Manager starts.")}
          </p>
        </div>
        <button
          onClick={() => handleToggle(true)}
          className="px-8 md:px-12 py-5 md:py-6 bg-ps-blue text-white text-lg md:text-2xl font-extrabold rounded-2xl md:rounded-[1.5rem] hover:bg-ps-blue/80 transition-all transform active:scale-95 shadow-[0_0_40px_rgba(0,149,255,0.3)]"
        >
          {t("autoload.enable_btn", "Enable Autoload")}
        </button>
      </div>
    )
  }

  return (
    <div className="space-y-6 h-full flex flex-col min-h-0">
      {/* [CN-FORK] v0.5.2.99 与其他菜单页同款的页级大标题：标题在左、「停用自启」在右。
       * 保存状态只在真的在保存/刚保存完时才渲染 desc —— 常驻的 h-6 占位会把页头撑得
       * 比其他菜单页高一截（用户反馈「太高了」），空闲时必须与无 desc 的页头等高。 */}
      <PageHead
        title={t("autoload.page_title", "Autoload")}
        desc={
          saving || saved ? (
            <div className="h-6 overflow-hidden">
              {saving ? (
                <div className="flex items-center text-ps-blue/60 text-xs font-bold uppercase tracking-widest">
                  <RefreshCw className="w-3 h-3 mr-2" />
                  <span>{t("autoload.saving", "Saving Changes...")}</span>
                </div>
              ) : (
                <div className="flex items-center text-emerald-500 text-xs font-bold uppercase tracking-widest">
                  <CheckCircle2 className="w-3 h-3 mr-2" />
                  <span>{t("autoload.saved", "All Changes Saved")}</span>
                </div>
              )}
            </div>
          ) : undefined
        }
        actions={
          <button onClick={() => handleToggle(false)} className="btn btn-danger">
            {t("autoload.disable_btn", "Disable Autoload")}
          </button>
        }
      />
      <div className="flex-1 flex flex-col min-h-0">
        <div className={cn(
          "gap-6 h-full min-h-0",
          isPS5 ? "grid grid-cols-2" : "hidden lg:grid lg:grid-cols-2"
        )}>
          {renderAvailable()}
          {renderSequence()}
        </div>
        <div className={cn(
          "h-full flex flex-col min-h-0",
          isPS5 ? "hidden" : "lg:hidden"
        )}>
          {subView === 'list' ? renderSequence() : renderAvailable()}
        </div>
      </div>

      <Modal
        show={showDelayModal}
        title={t("autoload.delay_modal.title", "Configure Delay")}
        onClose={() => setShowDelayModal(false)}
        footer={
          <button
            onClick={() => setShowDelayModal(false)}
            className="btn btn-outline w-full uppercase tracking-tight"
          >
            {t("autoload.delay_modal.cancel", "Cancel")}
          </button>
        }
      >
        <div className="space-y-6 md:space-y-8">
          <div className="grid grid-cols-3 gap-3 md:gap-4">
            {[1, 3, 5].map(s => (
              <button
                key={s}
                onClick={() => addDelay(s * 1000)}
                className="py-4 md:py-6 bg-ps-blue/20 hover:bg-ps-blue border border-ps-blue/30 text-white rounded-2xl font-black text-xl md:text-2xl transition-all shadow-lg"
              >
                {s}s
              </button>
            ))}
          </div>

          <div className="space-y-3 md:space-y-4">
            <p className="label-caps !text-zinc-500 text-sm md:text-base">{t("autoload.delay_modal.custom_delay_label", "Custom Delay (ms)")}</p>
            {/* [CN-FORK] v0.5.2.93 用 .stack-x（margin 实现的一排）替代 flex gap：老内核不认 flex gap，
                原来这一行在 PS5 上是输入框和「添加」贴在一起 */}
            <div className="stack-x">
              <input
                type="number"
                placeholder={t("autoload.delay_modal.placeholder", "e.g. 2500")}
                value={customDelay}
                onChange={(e) => setCustomDelay(e.target.value)}
                className="flex-1 field text-white font-mono text-xl md:text-2xl rounded-2xl"
              />
              <button
                onClick={() => customDelay && addDelay(parseInt(customDelay))}
                className="py-4 md:py-0 px-8 md:px-10 bg-ps-blue text-white rounded-2xl font-black uppercase italic tracking-tighter text-lg md:text-xl shadow-2xl hover:bg-ps-blue/80 transition-all shrink-0"
              >
                {t("autoload.delay_modal.add_btn", "Add")}
              </button>
            </div>
          </div>
        </div>
      </Modal>
    </div>
  )
}

export default AutoloadView
