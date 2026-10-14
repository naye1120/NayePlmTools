/* [CN-FORK] v0.6.0 合入：导入取两侧并集 —— 上游补了 apiFetch/mutationFetch/useCallback
 * 与 ArrowLeft/HardDrive/Cpu/Loader2（文件本体的刷新链就是上游那版），fork 补了
 * RefreshCw（上面重试按钮在用）与共用 UI 组件 PageHead/Section/LoadingState。 */
import { apiFetch, mutationFetch } from '../../utils/mutationRequest'
import React, { useState, useEffect, useCallback } from 'react'
import { ArrowLeft, HardDrive, Cpu, AlertTriangle, CheckCircle2, Loader2, Info, Usb, RefreshCw } from 'lucide-react'
import { useTranslation } from 'react-i18next'
import PageHead from '../ui/PageHead' /* [CN-FORK] v0.5.2.94 页头/板块/加载归一 */
import Section from '../ui/Section'
import LoadingState from '../ui/LoadingState'
/* [CN-FORK] v0.5.2.102 按功能分色（样张 A + I2）：重试＝青（btn-info，信息工具类），
 * 复制/覆盖写入＝金（btn-warn），「移动到内部」仍是本页唯一主操作（蓝），返回/取消＝紫。
 * 上游 0.6.0 把按钮组改成了「替换&复制 / 替换&移动 / 保留两者&复制 / 保留两者&移动」四条
 * （后端 slot_commit_file 支持 keep_both，汉语文案走上游内联的 locales/zh-CN.json），
 * 本页就这样统一，.btn 那几处留在上面重试/返回按钮上。 */

const MoveFromUsbView = ({ path, onBack, onComplete, addToast }) => {
  const { t } = useTranslation()
  const [status, setStatus] = useState('loading') // loading, confirm, exists_same, exists_different, processing, error, success
  const [details, setDetails] = useState(null)
  const [errorMsg, setErrorMsg] = useState('')

  const checkPayload = useCallback(async () => {
    setStatus('loading')
    try {
      const res = await apiFetch(`/usb_move_check?path=${encodeURIComponent(path)}`)
      const data = await res.json()
      if (data.error) {
        setErrorMsg(data.error)
        setStatus('error')
      } else {
        setDetails(data)
        if (data.status === 'exists_same' || data.status === 'exists_different' || data.folder_exists || data.existing) {
          setStatus('exists_different')
        } else {
          setStatus('confirm')
        }
      }
    } catch {
      setErrorMsg(t("move_usb.conn_failed", "Failed to connect to backend"))
      setStatus('error')
    }
  }, [path, t])

  useEffect(() => {
    // eslint-disable-next-line react-hooks/set-state-in-effect
    checkPayload()
  }, [checkPayload])

  const performMove = async (mode = 'new', keepOriginal = false) => {
    setStatus('processing')
    try {
      const modeParam = (mode === true || mode === 'replace') ? 'replace' : (mode === 'keep_both' ? 'keep_both' : 'new')
      const res = await mutationFetch(`/usb_move_perform?path=${encodeURIComponent(path)}&mode=${encodeURIComponent(modeParam)}&keep_original=${keepOriginal}`)
      const data = await res.json()
      if (data.error) {
        setErrorMsg(data.error)
        setStatus('error')
      } else {
        setStatus('success')
        addToast(data.warning || (keepOriginal ? t("move_usb.copied", "Payload copied to internal memory") : t("move_usb.moved", "Payload moved to internal memory")))
        setTimeout(() => {
          onComplete()
        }, 2000)
      }
    } catch {
      setErrorMsg(t("move_usb.op_failed", "Operation failed"))
      setStatus('error')
    }
  }

  return (
    /* [CN-FORK] v0.5.2.99 容器全站统一：去掉 max-w-4xl 居中与 pb-20，正文吃满 main 内容列，
     * 页头恢复出血，板块间距交给根节点的 space-y-6。 */
    <div className="space-y-6">
      <PageHead
        title={<>{t("move_usb.title_1", "Import")} <span className="text-ps-blue">{t("move_usb.title_2", "to Internal")}</span></>}
        back={onBack}
        backLabel={t("move_usb.back_btn", "Back to Management")}
      />

      <Section icon={Usb} title={t("move_usb.source_path_label", "Source Path")} bodyClass="space-y-5">
        <div className="flex items-center min-w-0">
          <div className="p-3 md:p-4 shrink-0 -ml-2">
            <Usb className="w-6 h-6 md:w-7 md:h-7 text-ps-blue" />
          </div>
          <div className="min-w-0 flex-1 ml-3">
            <p className="label-caps !text-ps-blue">{t("move_usb.source_path_label", "Source Path")}</p>
            <p className="text-base md:text-lg font-black text-white italic tracking-tight truncate w-full">{path}</p>
          </div>
        </div>

        <div className="h-px bg-white/5" />

        {status === 'loading' && (
          <LoadingState label={t("move_usb.checking_storage", "Checking internal storage...")} />
        )}

        {status === 'error' && (
          <div className="p-5 bg-red-500/10 border border-red-500/20 rounded-xl flex items-start">
            <AlertTriangle className="w-6 h-6 text-red-500 shrink-0" />
            <div className="ml-3">
              <p className="text-base font-bold text-white">{t("move_usb.error_title", "Something went wrong")}</p>
              <p className="text-red-400/80 leading-relaxed text-sm mt-1">{errorMsg}</p>
              <button onClick={checkPayload} className="btn btn-info mt-4">
                <RefreshCw className="w-4 h-4" />
                <span>{t("move_usb.retry_btn", "Retry")}</span>
              </button>
            </div>
          </div>
        )}

        {status === 'exists_same' && (
          <div className="space-y-5">
            <div className="p-5 md:p-6 bg-emerald-500/10 border border-emerald-500/20 rounded-xl flex flex-col md:flex-row items-center md:items-start text-center md:text-left">
              <CheckCircle2 className="w-7 h-7 text-emerald-500 shrink-0" />
              <div className="md:ml-4 mt-3 md:mt-0">
                <p className="text-base font-bold text-white">{t("move_usb.same_exists_title", "Identical file already exists")}</p>
                <p className="text-sm md:text-base text-emerald-400/80 leading-relaxed mt-1">
                  {t("move_usb.same_exists_desc", "A payload with the same name and content (SHA256: {{sha}}...) is already in your internal library. No action is needed.", { sha: details?.sha256?.substring(0, 12) })}
                </p>
              </div>
            </div>
            <button onClick={onBack} className="btn btn-outline w-full">{t("move_usb.return_btn", "Return to Storage Hub")}</button>
          </div>
        )}

        {status === 'exists_different' && (
          <div className="space-y-5">
            <div className="p-5 md:p-6 bg-amber-500/10 border border-amber-500/20 rounded-xl flex flex-col md:flex-row items-center md:items-start text-center md:text-left">
              <AlertTriangle className="w-7 h-7 text-amber-500 shrink-0" />
              <div className="md:ml-4 mt-3 md:mt-0">
                <p className="text-base font-bold text-white">{t("move_usb.prev_version_title", "Previous version detected")}</p>
                <p className="text-sm md:text-base text-amber-400/80 leading-relaxed mt-1">
                  {t("move_usb.prev_version_desc", "A version of <1>{{name}}</1> already exists in internal storage. Importing this will replace the current installation. Do you want to proceed?", { components: { 1: <strong /> }, name: details?.folder_name || details?.filename })}
                </p>
              </div>
            </div>
            {/* [CN-FORK] v0.6.0 合入：按钮组取上游侧 —— 它多给了一条「保留两者」（后端
                slot_commit_file 支持 keep_both），且文案键走上游内联的 locales/zh-CN.json
                （fork 的 locales-overlay 里没有 move_usb.*，走旧键只会落英语默认值）。 */}
            <div className="flex flex-col gap-3">
              <div className="flex flex-col sm:flex-row gap-3">
                <button onClick={() => performMove('replace', true)} className="flex-1 py-3.5 md:py-4 rounded-xl bg-amber-600/80 hover:bg-amber-600 text-white font-black uppercase italic text-sm transition-all shadow-md">
                  {t("move_usb.replace_copy_btn", "Replace & Copy")}
                </button>
                <button onClick={() => performMove('replace', false)} className="flex-1 py-3.5 md:py-4 rounded-xl bg-amber-600 hover:bg-amber-500 text-white font-black uppercase italic text-sm transition-all shadow-lg">
                  {t("move_usb.replace_move_btn", "Replace & Move")}
                </button>
              </div>
              <div className="flex flex-col sm:flex-row gap-3">
                <button onClick={() => performMove('keep_both', true)} className="flex-1 py-3.5 md:py-4 rounded-xl bg-ps-blue/70 hover:bg-ps-blue/90 text-white font-black uppercase italic text-sm transition-all shadow-md">
                  {t("move_usb.keep_both_copy_btn", "Keep both & Copy")}
                </button>
                <button onClick={() => performMove('keep_both', false)} className="flex-1 py-3.5 md:py-4 rounded-xl bg-ps-blue hover:bg-ps-blue/80 text-white font-black uppercase italic text-sm transition-all shadow-lg shadow-ps-blue/20">
                  {t("move_usb.keep_both_move_btn", "Keep both & Move")}
                </button>
              </div>
              <button onClick={onBack} className="w-full py-3 rounded-xl bg-white/5 hover:bg-white/10 text-white font-bold uppercase text-xs transition-all border border-white/10">
                {t("move_usb.cancel_btn", "Cancel")}
              </button>
            </div>
          </div>
        )}

        {status === 'confirm' && (
          <div className="space-y-5">
            <div className="p-5 md:p-6 bg-ps-blue/5 border border-ps-blue/10 rounded-xl flex flex-col md:flex-row items-center md:items-start text-center md:text-left">
              <Info className="w-7 h-7 text-ps-blue shrink-0" />
              <div className="md:ml-4 mt-3 md:mt-0">
                <p className="text-base font-bold text-white">{t("move_usb.ready_title", "Ready to Import")}</p>
                <p className="text-sm md:text-base text-zinc-400 leading-relaxed mt-1">
                  {t("move_usb.ready_desc", "The payload will be imported to internal storage. You can either copy it and keep the original on the USB, or move it and delete the original.")}
                </p>
              </div>
            </div>
            <div className="stack-x">
              <button onClick={() => performMove(false, true)} className="btn btn-warn flex-1">{t("move_usb.copy_btn", "Copy to Internal")}</button>
              <button onClick={() => performMove(false, false)} className="btn btn-primary flex-1">{t("move_usb.move_btn", "Move to Internal")}</button>
            </div>
          </div>
        )}

        {status === 'processing' && (
          <div className="py-12 flex flex-col items-center justify-center text-center">
            <div className="ps5-robust-spinner" />
            <div className="mt-6">
              <p className="text-xl font-bold text-white">{t("move_usb.importing", "Importing Payload...")}</p>
              <p className="text-zinc-500 text-sm mt-1">{t("move_usb.copying_desc", "Copying data and verifying integrity.")}</p>
            </div>
          </div>
        )}

        {status === 'success' && (
          <div className="py-12 flex flex-col items-center justify-center text-center">
            <div className="w-16 h-16 bg-emerald-500 rounded-full flex items-center justify-center">
              <CheckCircle2 className="w-9 h-9 text-white" />
            </div>
            <div className="mt-5">
              <p className="text-xl font-bold text-white">{t("move_usb.success_title", "Success!")}</p>
              <p className="text-zinc-500 text-sm mt-1">{t("move_usb.success_desc", "The payload has been safely imported to internal storage.")}</p>
            </div>
          </div>
        )}
      </Section>
    </div>
  )
}

export default MoveFromUsbView
