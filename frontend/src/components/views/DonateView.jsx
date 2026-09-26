import React from 'react'
import { Heart } from 'lucide-react'
import { QRCodeSVG } from 'qrcode.react'
import { useTranslation } from 'react-i18next'
import { isPS5 } from '../../utils/helpers'

const DonateView = ({ version }) => {
  const { t } = useTranslation()
  const donateUrl = 'https://github.com/itsPLK/ps5-payload-manager/blob/main/DONATE.md';
  const upstreamRepoUrl = 'https://github.com/itsPLK/ps5-payload-manager';
  return (
    <div className="animate-fade-in max-w-6xl mx-auto py-10 xl:py-20 flex flex-col items-center gap-12 xl:gap-16">
      <div className="flex flex-col xl:flex-row items-center justify-center w-full min-h-[40vh] gap-12 xl:gap-24">
        {/* Left Column */}
        <div className="flex flex-col items-center text-center space-y-8 md:space-y-10 flex-1 px-4">
          <div className="p-8 md:p-10 rounded-[2.5rem] md:rounded-[3.5rem] bg-red-600/10 border border-red-600/20 text-red-500 shadow-2xl inline-flex">
            <Heart className="w-16 h-16 md:w-20 md:h-20 fill-current animate-pulse" />
          </div>
          <div className="space-y-4 md:space-y-6">
            <h3 className="text-2xl md:text-4xl font-black text-white uppercase italic tracking-tighter">
              {t("donate.title_1", "Support")} <span className="text-red-500">{t("donate.title_2", "Project")}</span>
            </h3>
            <p className="text-base md:text-lg text-zinc-400 font-medium leading-relaxed italic">
              {t("donate.description", "If you'd like to support my work or just say thanks, you can buy me a coffee. It's much appreciated!")}
            </p>
          </div>
        </div>

        {/* Right Column */}
        <div className="flex items-center justify-center flex-1 px-4">
          {isPS5 ? (
            <div className="bg-white p-6 md:p-8 rounded-[2rem] md:rounded-[3rem] shadow-[0_0_60px_rgba(220,38,38,0.3)]">
              <QRCodeSVG value={donateUrl} size={isPS5 ? 280 : 180} level="H" />
            </div>
          ) : (
            <a
              href={donateUrl}
              target="_blank"
              rel="noopener noreferrer"
              className="px-8 md:px-12 py-4 md:py-5 bg-red-600 text-white text-base md:text-xl font-black uppercase rounded-2xl hover:bg-red-500 transition-all transform active:scale-95 shadow-[0_0_50px_rgba(220,38,38,0.4)] text-center block"
            >
              {t("donate.view_options_btn", "View Donation Options")}
            </a>
          )}
        </div>
      </div>

      {/* [CN-FORK] 关于本 Fork：原作者、上游地址、程序名、上游/本 fork 版本与维护者 naye 署名 */}
      <div className="w-full max-w-2xl glass-card p-5 md:p-6 rounded-ps-2xl border border-white/10 space-y-3 text-left">
        <p className="label-caps !text-ps-blue !opacity-100">{t("donate.fork_title", "About This Fork")}</p>
        <p className="text-sm text-zinc-300 leading-relaxed">
          {t("donate.fork_desc", "This is a fork of Payload Manager maintained by {{maintainer}}.", { maintainer: "naye" })}
        </p>
        <dl className="grid grid-cols-[auto_1fr] gap-x-4 gap-y-1.5 text-sm">
          <dt className="text-zinc-500">{t("donate.fork_original_author", "Original author")}</dt>
          <dd className="text-white font-semibold">itsPLK</dd>
          <dt className="text-zinc-500">{t("donate.fork_app_name", "Application")}</dt>
          <dd className="text-white font-semibold">Payload Manager</dd>
          <dt className="text-zinc-500">{t("donate.fork_upstream", "Upstream")}</dt>
          <dd>
            <a href={upstreamRepoUrl} target="_blank" rel="noopener noreferrer" className="text-ps-blue hover:underline break-all">
              github.com/itsPLK/ps5-payload-manager
            </a>
          </dd>
          <dt className="text-zinc-500">{t("donate.fork_upstream_version", "Upstream version")}</dt>
          <dd className="text-white font-semibold">v0.5.1</dd>
          <dt className="text-zinc-500">{t("donate.fork_version", "This fork")}</dt>
          <dd className="text-white font-semibold">v{version || '0.5.1.7'}</dd>
        </dl>
        <p className="text-xs text-zinc-500 leading-relaxed">
          {t("donate.fork_donate_note", "Donations through the button above go to the original author (itsPLK), not this fork.")}
        </p>
      </div>
    </div>
  )
}

export default DonateView
