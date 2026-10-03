import React, { useState, useEffect, useRef } from 'react'
import { useTranslation } from 'react-i18next'
import { RefreshCw, ExternalLink } from 'lucide-react'

/* [CN-FORK] 「PKG安装」视图：把 PKG Manager 自带界面（:8844）内嵌到右侧内容区，侧栏保留。
 * 进入时查状态：未运行则自动拉起并轮询等待就绪（最多 ~20s），就绪后渲染 iframe。
 * 可行前提：PKG Manager 响应不设 X-Frame-Options/CSP（已核对其源码），且浏览器 cookie
 * 不区分端口，其登录态在 iframe 内正常。上传更新等低频操作在设置页管理卡片。 */

/* [CN-FORK] v0.5.2.85 按钮收口到 index.css 骨架层（描边档） */
const BTN_GHOST = 'btn btn-outline'

export default function PkgManagerFrame() {
  const { t } = useTranslation()
  const [phase, setPhase] = useState('checking') // checking | starting | ready | failed
  const [tick, setTick] = useState(0) // 失败后点「重试」重新走启动流程
  const aliveRef = useRef(true)

  useEffect(() => {
    aliveRef.current = true
    const sleep = (ms) => new Promise((r) => setTimeout(r, ms))
    const getStatus = () =>
      fetch(`/pkgmanager_status?_=${Date.now()}`, { cache: 'no-store' }).then((r) => r.json()).catch(() => null)

    ;(async () => {
      setPhase('checking')
      let st = await getStatus()
      if (st && !st.running) {
        setPhase('starting')
        const lr = await fetch(`/pkgmanager_launch?_=${Date.now()}`, { cache: 'no-store' })
          .then((r) => r.json()).catch(() => null)
        if (lr && !lr.ok) {
          if (aliveRef.current) setPhase('failed')
          return
        }
        for (let i = 0; i < 40; i++) {
          await sleep(500)
          if (!aliveRef.current) return
          st = await getStatus()
          if (st && st.running) break
        }
      }
      if (aliveRef.current) setPhase(st && st.running ? 'ready' : 'failed')
    })()

    return () => { aliveRef.current = false }
  }, [tick])

  const url = `http://${window.location.hostname || '127.0.0.1'}:8844`

  if (phase === 'checking' || phase === 'starting') {
    return (
      <div className="flex flex-col items-center justify-center gap-4 py-32 text-zinc-400">
        <RefreshCw className="w-6 h-6 animate-spin text-ps-blue" />
        <p className="text-[14px] font-bold">
          {phase === 'checking' ? t('pkg.frame_checking', '检查 PKG Manager 状态…') : t('pkg.opening', '启动中…')}
        </p>
        <p className="text-[12px] text-zinc-500">{t('pkg.frame_first_run', '首次启动需要几秒，请稍候')}</p>
      </div>
    )
  }

  if (phase === 'failed') {
    return (
      <div className="flex flex-col items-center justify-center gap-4 py-32">
        <p className="text-[14px] font-bold text-red-400">
          {t('pkg.open_fail', 'PKG Manager 启动失败')}
        </p>
        <p className="text-[12px] text-zinc-500 max-w-md text-center leading-relaxed">
          {t('pkg.frame_fail_hint', '请确认 elfldr 已在 9021 端口运行；也可到设置页的「PKG Manager」卡片查看状态。')}
        </p>
        <button onClick={() => setTick((x) => x + 1)} className={BTN_GHOST}>
          <RefreshCw className="w-4 h-4" />
          {t('pkg.frame_retry', '重试')}
        </button>
      </div>
    )
  }

  /* 就绪：iframe 铺满整个右侧内容区（main 在 pkg 视图已去内边距），
   * 新标签页入口缩为右下角悬浮小按钮，避免遮挡 PKG Manager 自己的顶栏操作区 */
  return (
    <div className="relative flex-1 min-h-0 flex flex-col">
      <iframe
        src={url}
        title="PKG Manager"
        className="w-full flex-1 min-h-0 border-0 bg-white"
      />
      <a
        href={url}
        target="_blank"
        rel="noreferrer"
        title={t('pkg.frame_open_new', '新标签页打开')}
        className="absolute bottom-5 right-5 z-10 p-3 rounded-full bg-black/60 text-white/70 hover:text-white hover:bg-black/80 transition"
      >
        <ExternalLink className="w-4 h-4" />
      </a>
    </div>
  )
}
