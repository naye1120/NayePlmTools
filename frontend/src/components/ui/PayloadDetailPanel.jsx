import React, { useState, useEffect, useRef } from 'react';
import {
  CloudDownload,
  Loader2,
  ArrowLeft,
  ArrowUpCircle,
  CheckCircle2,
  CircleUser,
  ExternalLink,
  FileText,
  Sparkles,
  Trash2,
  Globe,
} from 'lucide-react';
import { parseMarkdown } from '../../utils/markdown';
import { isGitHubUrl } from '../../utils/payloadStatus';
import { useTranslation } from 'react-i18next';
import PayloadIcon from './PayloadIcon';
import Modal from './Modal';

const GithubIcon = ({ className = "w-4 h-4" }) => (
  <svg className={className} viewBox="0 0 24 24" fill="currentColor">
    <path
      fillRule="evenodd"
      clipRule="evenodd"
      d="M12 2C6.477 2 2 6.484 2 12.017c0 4.425 2.865 8.18 6.839 9.504.5.092.682-.217.682-.483 0-.237-.008-.868-.013-1.703-2.782.605-3.369-1.343-3.369-1.343-.454-1.158-1.11-1.466-1.11-1.466-.908-.62.069-.608.069-.608 1.003.07 1.53 1.032 1.53 1.032.892 1.53 2.341 1.088 2.91.832.092-.647.35-1.088.636-1.338-2.22-.253-4.555-1.113-4.555-4.951 0-1.093.39-1.988 1.029-2.688-.103-.253-.446-1.272.098-2.65 0 0 .84-.27 2.75 1.026A9.564 9.564 0 0112 6.844c.85.004 1.705.115 2.504.337 1.909-1.296 2.747-1.027 2.747-1.027.546 1.379.202 2.398.1 2.651.64.7 1.028 1.595 1.028 2.688 0 3.848-2.339 4.695-4.566 4.943.359.309.678.92.678 1.855 0 1.338-.012 2.419-.012 2.747 0 .268.18.58.688.482A10.019 10.019 0 0022 12.017C22 6.484 17.522 2 12 2z"
    />
  </svg>
);

const getRepoUrl = (p) => {
  if (!p) return null;
  if (p.repository) return p.repository;
  if (p.repo_url) return p.repo_url;
  if (p.source) {
    return p.source.replace(/\/releases(\/.*)?$/, '');
  }
  if (p.source_direct) {
    return p.source_direct.replace(/\/releases(\/.*)?$/, '');
  }
  return null;
};

const getReadmeUrls = (payload, srcUrl) => {
  const urls = [];

  // 1. Explicit details_url
  if (payload?.details_url) {
    let url = payload.details_url;
    if (typeof url === 'string' && !url.startsWith('http') && srcUrl) {
      const base = srcUrl.substring(0, srcUrl.lastIndexOf('/') + 1);
      url = base + url;
    }
    urls.push(url);
  }

  // 2. GitHub repo README fallback
  const repo = getRepoUrl(payload);
  if (repo) {
    const match = repo.match(/^https?:\/\/(?:www\.)?github\.com\/([^/]+)\/([^/]+)/i);
    if (match) {
      const owner = match[1];
      const repoName = match[2].replace(/\.git$/, '');
      urls.push(`https://raw.githubusercontent.com/${owner}/${repoName}/HEAD/README.md`);
      urls.push(`https://raw.githubusercontent.com/${owner}/${repoName}/main/README.md`);
      urls.push(`https://raw.githubusercontent.com/${owner}/${repoName}/master/README.md`);
    }
  }

  return urls;
};

const PayloadDetailPanel = ({
  payload,
  onClose,
  onInstall,
  onDelete,
  statusInfo,
  isInstalled,
  isUpdate,
  isInstalling = false,
  srcUrl,
  srcId,
  externalLinkUrl: propExternalLinkUrl,
  setExternalLinkUrl: propSetExternalLinkUrl,
}) => {
  const { t } = useTranslation();
  const [detailsHtml, setDetailsHtml] = useState(null);
  const [detailsError, setDetailsError] = useState(false);
  const [loading, setLoading] = useState(false);
  const [localLinkUrl, setLocalLinkUrl] = useState(null);
  const detailsRef = useRef(null);

  const linkModalUrl = propExternalLinkUrl !== undefined ? propExternalLinkUrl : localLinkUrl;
  const setLinkModalUrl = propSetExternalLinkUrl || setLocalLinkUrl;

  const repoUrl = getRepoUrl(payload);
  const installedSlot = statusInfo?.installedSlot?.slot;
  const canRemove = Boolean(isInstalled && installedSlot && onDelete);

  const handleMarkdownClick = (e) => {
    const link = e.target.closest('a');
    if (!link) return;
    const href = link.getAttribute('href');
    if (!href) return;
    if (href.startsWith('#')) return;

    e.preventDefault();
    e.stopPropagation();
    setLinkModalUrl(href);
  };

  useEffect(() => {
    const urls = getReadmeUrls(payload, srcUrl);
    if (urls.length === 0) {
      return;
    }

    let cancelled = false;
    const fetchDetails = async () => {
      setDetailsHtml(null);
      setDetailsError(false);
      setLoading(true);
      let foundText = null;

      for (const url of urls) {
        try {
          const res = await fetch(url);
          if (res.ok) {
            const text = await res.text();
            if (text && !text.trim().startsWith('<!DOCTYPE') && !text.trim().startsWith('<html')) {
              foundText = text;
              break;
            }
          }
        } catch {
          /* try next candidate */
        }
      }

      if (cancelled) return;

      if (foundText) {
        const cleanHtml = parseMarkdown(foundText);
        setDetailsHtml(cleanHtml);
      } else {
        setDetailsError(true);
      }
      setLoading(false);
    };

    fetchDetails();
    return () => {
      cancelled = true;
    };
  }, [payload, srcUrl]);

  if (!payload) return null;

  return (
    <div className="flex flex-col w-full min-h-full min-w-0 max-w-full animate-in fade-in duration-300 pb-16">
      {/* Back button */}
      <div className="flex items-center justify-between mb-6">
        <button
          type="button"
          onClick={onClose}
          className="flex items-center space-x-2.5 text-zinc-400 hover:text-white transition-colors bg-white/5 hover:bg-white/10 px-4 py-2 rounded-xl border border-white/5 hover:border-white/10"
        >
          <ArrowLeft className="w-5 h-5" />
          <span className="font-bold text-sm uppercase tracking-wider">
            {t("storage_hub.back_to_repository", "Back to Repository")}
          </span>
        </button>
      </div>

      {/* Hero Header Card */}
      <div className="glass-card p-6 md:p-8 rounded-3xl border border-white/10 flex flex-col md:flex-row items-start gap-6 relative min-w-0 max-w-full overflow-hidden">
        {/* Left: Big Icon */}
        <div className="shrink-0 self-start">
          <PayloadIcon
            icon={payload.icon}
            name={payload.filename}
            className="w-20 h-20 sm:w-24 sm:h-24 md:w-28 md:h-28 text-3xl sm:text-4xl shadow-2xl"
          />
        </div>

        {/* Center: Details & Metadata */}
        <div className="flex-1 min-w-0 flex flex-col justify-start gap-3 w-full">
          {/* Title + Version pill */}
          <div className="flex flex-wrap items-center gap-2.5 sm:gap-3">
            <h1 className="text-2xl sm:text-3xl md:text-4xl font-extrabold text-white tracking-tight break-words">
              {payload.name || payload.filename}
            </h1>
            {payload.version && (
              <span className="px-2.5 py-1 rounded-lg bg-ps-blue/15 text-ps-blue border border-ps-blue/30 font-extrabold text-xs sm:text-sm tracking-wider uppercase shrink-0">
                {String(payload.version).startsWith('v') ? payload.version : `v${payload.version}`}
              </span>
            )}
            {isInstalled && (
              <span className="px-2.5 py-1 rounded-lg bg-emerald-500/15 text-emerald-400 border border-emerald-500/30 font-extrabold text-xs sm:text-sm tracking-wider uppercase inline-flex items-center gap-1.5 shrink-0">
                <CheckCircle2 className="w-3.5 h-3.5" />
                <span>{t("payload_detail.installed", "Installed")}</span>
              </span>
            )}
            {isUpdate && !isInstalled && (
              <span className="px-2.5 py-1 rounded-lg bg-ps-blue/20 text-ps-blue border border-ps-blue/40 font-extrabold text-xs sm:text-sm tracking-wider uppercase inline-flex items-center gap-1.5 shrink-0">
                <ArrowUpCircle className="w-3.5 h-3.5" />
                <span>{t("storage_hub.update_badge", "Update")}</span>
              </span>
            )}
            {payload.category && (
              <span className="px-2.5 py-1 rounded-lg bg-white/5 text-zinc-400 border border-white/10 font-bold text-xs uppercase tracking-wider shrink-0">
                {t(`category.${payload.category}`, payload.category)}
              </span>
            )}
          </div>

          {statusInfo?.restartRequired && (
            <p className="text-sm text-emerald-400">
              {t("app.toasts.self_update_downloaded", "Update downloaded — applies on next start")}
            </p>
          )}

          {/* Author & Repository Metadata */}
          <div className="flex flex-wrap items-center gap-y-2 gap-x-4 text-xs sm:text-sm text-zinc-400">
            {payload.author && (
              <div className="flex items-center gap-1.5 shrink-0">
                <CircleUser className="w-4 h-4 text-zinc-400 shrink-0" />
                <span>
                  {t("storage_hub.by_author", "by {{author}}", { author: payload.author })}
                </span>
              </div>
            )}
            {repoUrl && (() => {
              const isGithub = isGitHubUrl(repoUrl);
              const displayRepo = isGithub
                ? repoUrl.replace(/^https?:\/\/(?:www\.)?github\.com\//i, '').replace(/\/$/, '')
                : repoUrl.replace(/^https?:\/\/(?:www\.)?/i, '').replace(/\/$/, '');
              return (
                <a
                  href={repoUrl}
                  target="_blank"
                  rel="noopener noreferrer"
                  onClick={(e) => {
                    e.preventDefault();
                    setLinkModalUrl(repoUrl);
                  }}
                  className="flex items-center gap-1.5 text-ps-blue hover:text-white transition-colors underline-offset-2 hover:underline shrink-0 max-w-full"
                  title={repoUrl}
                >
                  {isGithub ? (
                    <GithubIcon className="w-4 h-4 shrink-0" />
                  ) : (
                    <Globe className="w-4 h-4 shrink-0" />
                  )}
                  <span className="truncate max-w-[240px] sm:max-w-[320px]">
                    {displayRepo}
                  </span>
                  <ExternalLink className="w-3.5 h-3.5 shrink-0 opacity-70" />
                </a>
              );
            })()}
            {payload.filename && (
              <span className="font-mono text-zinc-500 text-xs truncate max-w-[220px]" title={payload.filename}>
                {payload.filename}
              </span>
            )}
          </div>

          {/* Short description */}
          {payload.description && (
            <p className="text-sm sm:text-base text-zinc-300 leading-relaxed max-w-3xl pt-1">
              {payload.description}
            </p>
          )}
          {statusInfo?.alsoInstalledFrom && (
            <p className="text-xs sm:text-sm text-amber-400/90 font-medium pt-1">
              {t("storage_hub.also_installed_hint", "Also installed from {{source}}", { source: statusInfo.alsoInstalledFrom })}
            </p>
          )}
        </div>

        {/* Action Button: Compact, nicely proportioned */}
        <div className="shrink-0 w-full sm:w-auto md:self-start pt-1 flex flex-col gap-2">
          {(() => {
            const buttonAction = statusInfo ? statusInfo.buttonAction : (isUpdate ? 'update' : (isInstalled ? 'reinstall' : 'install'));

            if (isInstalling) {
              const label = (buttonAction === 'update' || isUpdate)
                ? t("payload_detail.updating", "Updating...")
                : t("payload_detail.installing", "Installing...");
              return (
                <button
                  type="button"
                  disabled
                  className="w-full sm:w-auto inline-flex items-center justify-center space-x-2.5 px-7 py-3.5 rounded-2xl font-extrabold text-base transition-all bg-ps-blue/60 text-white cursor-not-allowed opacity-90 shadow-lg"
                >
                  <Loader2 className="w-5 h-5 shrink-0 animate-spin" />
                  <span>{label}</span>
                </button>
              );
            }

            if (buttonAction === 'update') {
              return (
                <button
                  type="button"
                  onClick={() => onInstall(payload, srcId, srcUrl, statusInfo)}
                  className="w-full sm:w-auto inline-flex items-center justify-center space-x-2.5 px-7 py-3.5 rounded-2xl font-extrabold text-base transition-all active:scale-95 shadow-lg bg-emerald-600 hover:bg-emerald-500 text-white shadow-emerald-950/40"
                >
                  <ArrowUpCircle className="w-5 h-5 shrink-0" />
                  <span>{t("payload_detail.update", "Update")}</span>
                </button>
              );
            }

            if (buttonAction === 'reinstall') {
              return null;
            }

            if (buttonAction === 'install') {
              return (
                <button
                  type="button"
                  onClick={() => onInstall(payload, srcId, srcUrl, statusInfo)}
                  className="w-full sm:w-auto inline-flex items-center justify-center space-x-2.5 px-7 py-3.5 rounded-2xl font-extrabold text-base transition-all active:scale-95 shadow-lg bg-ps-blue hover:bg-ps-blue/80 text-white shadow-ps-blue/20"
                >
                  <CloudDownload className="w-5 h-5 shrink-0" />
                  <span>{t("payload_detail.install", "Install")}</span>
                </button>
              );
            }

            if (canRemove) return null;

            // The manager itself has no removable payload slot.
            return (
              <div className="w-full sm:w-auto inline-flex items-center justify-center space-x-2.5 px-7 py-3.5 rounded-2xl font-extrabold text-base bg-white/5 text-zinc-400 border border-white/10">
                <CheckCircle2 className="w-5 h-5 shrink-0 text-emerald-400" />
                <span>{t("payload_detail.installed", "Installed")}</span>
              </div>
            );
          })()}
          {canRemove && (
            <button type="button" disabled={isInstalling}
              onClick={() => onDelete(installedSlot, payload.name || payload.filename)}
              className="w-full sm:w-auto inline-flex items-center justify-center space-x-2.5 px-7 py-3.5 rounded-2xl font-extrabold text-base transition-colors bg-red-600/10 hover:bg-red-600 text-red-400 hover:text-white border border-red-500/30 disabled:opacity-50 disabled:pointer-events-none">
              <Trash2 className="w-5 h-5 shrink-0" />
              <span>{t("app.dashboard.removePayload", "Remove Payload")}</span>
            </button>
          )}
        </div>
      </div>

      {/* Release Notes / What's New */}
      {payload.release_notes && (
        <div className="mt-8 space-y-3 min-w-0 max-w-full">
          <h3 className="text-lg sm:text-xl font-bold text-white flex items-center gap-2">
            <Sparkles className="w-5 h-5 text-ps-blue" />
            <span>{t("payload_detail.whats_new", "What's New")}</span>
          </h3>
          <div className="glass-panel p-5 sm:p-6 rounded-2xl border border-white/5 bg-black/20 min-w-0 max-w-full overflow-hidden">
            <div
              className="markdown-body text-sm min-w-0 max-w-full"
              onClick={handleMarkdownClick}
              dangerouslySetInnerHTML={{ __html: parseMarkdown(payload.release_notes) }}
            />
          </div>
        </div>
      )}

      {/* Full Description / README Section */}
      <div className="mt-8 space-y-4 min-w-0 max-w-full">
        <div className="flex items-center justify-between border-b border-white/10 pb-3">
          <h3 className="text-lg sm:text-xl font-bold text-white flex items-center gap-2">
            <FileText className="w-5 h-5 text-ps-blue" />
            <span>{t("payload_detail.readme_title", "README")}</span>
          </h3>
          {repoUrl && (
            <a
              href={repoUrl}
              target="_blank"
              rel="noopener noreferrer"
              onClick={(e) => {
                e.preventDefault();
                setLinkModalUrl(repoUrl);
              }}
              className="text-xs text-zinc-400 hover:text-ps-blue flex items-center gap-1.5 transition-colors"
            >
              <span>
                {isGitHubUrl(repoUrl)
                  ? t("payload_detail.view_on_github", "View on GitHub")
                  : t("payload_detail.view_source", "View Source")}
              </span>
              <ExternalLink className="w-3.5 h-3.5" />
            </a>
          )}
        </div>

        {loading ? (
          <div className="py-16 glass-panel rounded-2xl border border-white/5 flex flex-col items-center justify-center space-y-3">
            <Loader2 className="w-8 h-8 text-ps-blue animate-spin" />
            <p className="text-zinc-400 text-sm">
              {t("payload_detail.loading_details", "Loading README...")}
            </p>
          </div>
        ) : detailsHtml ? (
          <div className="glass-panel p-6 sm:p-8 rounded-3xl border border-white/5 bg-black/20 min-w-0 max-w-full overflow-hidden">
            <div
              ref={detailsRef}
              className="markdown-body min-w-0 max-w-full"
              onClick={handleMarkdownClick}
              dangerouslySetInnerHTML={{ __html: detailsHtml }}
            />
          </div>
        ) : detailsError ? (
          <div className="py-12 glass-panel rounded-2xl border border-white/5 text-center text-zinc-500">
            <p className="text-sm">
              {t("payload_detail.no_readme", "No README available for this payload.")}
            </p>
          </div>
        ) : (
          <div className="py-12 glass-panel rounded-2xl border border-white/5 text-center text-zinc-500">
            <p className="text-sm">
              {t("payload_detail.no_readme", "No README available for this payload.")}
            </p>
          </div>
        )}
      </div>

      {/* External Link Confirmation Modal */}
      <Modal
        show={Boolean(linkModalUrl)}
        title={t("payload_detail.external_link_title", "Open External Link")}
        onClose={() => setLinkModalUrl(null)}
        footer={
          <div className="flex flex-col sm:flex-row w-full gap-3">
            <button
              type="button"
              onClick={() => setLinkModalUrl(null)}
              className="flex-1 py-3 px-4 rounded-xl bg-white/5 hover:bg-white/10 text-white font-bold uppercase text-sm transition-all"
            >
              {t("app.buttons.cancel", "Cancel")}
            </button>
            <a
              href={linkModalUrl || '#'}
              target="_blank"
              rel="noopener noreferrer"
              onClick={() => {
                setTimeout(() => {
                  setLinkModalUrl(null);
                }, 100);
              }}
              className="flex-1 inline-flex items-center justify-center py-3 px-4 rounded-xl bg-ps-blue hover:bg-ps-blue/80 text-white font-black uppercase text-sm transition-all shadow-lg"
            >
              {t("payload_detail.open_link", "Open Link")}
            </a>
          </div>
        }
      >
        <div className="space-y-4">
          <p>
            {t("payload_detail.external_link_confirm", "Are you sure you want to open this external link?")}
          </p>
          <div className="p-3 bg-black/40 rounded-xl border border-white/5 font-mono text-xs text-zinc-400 break-all select-all">
            {linkModalUrl}
          </div>
        </div>
      </Modal>
    </div>
  );
};

export default PayloadDetailPanel;
