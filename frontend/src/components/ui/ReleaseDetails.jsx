import { useEffect, useState } from 'react';
import { ChevronDown, Loader2, Sparkles } from 'lucide-react';
import { useTranslation } from 'react-i18next';
import { fetchReleaseNotes } from '../../utils/releaseNotes';
import { createRequestScope } from '../../utils/requestScope';
import { parseMarkdown } from '../../utils/markdown';

export default function ReleaseDetails({ payload, onMarkdownClick }) {
  const { t } = useTranslation();
  const [expanded, setExpanded] = useState(false);
  const [notes, setNotes] = useState(null);
  const [loading, setLoading] = useState(false);

  useEffect(() => {
    if (!expanded || notes !== null) return;
    const scope = createRequestScope();
    const load = async () => {
      setLoading(true);
      try {
        const markdown = await fetchReleaseNotes(payload, { signal: scope.signal });
        if (!scope.aborted) setNotes(markdown);
      } catch {
        // Leave notes unresolved so reopening can retry a failed request.
      } finally {
        if (!scope.aborted) setLoading(false);
      }
    };
    load();
    return () => scope.abort();
  }, [expanded, notes, payload]);

  return (
    <details className="group mt-8 glass-panel rounded-2xl border border-white/10 bg-black/20 min-w-0 max-w-full overflow-hidden"
      onToggle={event => setExpanded(event.currentTarget.open)}>
      <summary className="release-details-summary flex items-center gap-2 p-5 sm:p-6 list-none cursor-pointer text-lg sm:text-xl font-bold text-white focus-visible:outline-ps-blue">
        <Sparkles className="w-5 h-5 text-ps-blue shrink-0" />
        <span className="flex-1">{t('payload_detail.release_details', 'Release details')}</span>
        <ChevronDown className="w-5 h-5 shrink-0 group-open:rotate-180 transition-transform" />
      </summary>
      {expanded && <div className="px-5 pb-5 sm:px-6 sm:pb-6">
        {loading ? <div className="flex items-center gap-2 text-sm text-zinc-400" role="status">
          <Loader2 className="w-4 h-4 animate-spin" />
          {t('payload_detail.loading_release', 'Loading release details...')}
        </div> : notes ? <div className="markdown-body text-sm min-w-0 max-w-full"
          onClick={onMarkdownClick} dangerouslySetInnerHTML={{ __html: parseMarkdown(notes) }} />
          : <p className="text-sm text-zinc-500">{t('payload_detail.no_release_notes', 'No release notes available for this version.')}</p>}
      </div>}
    </details>
  );
}
