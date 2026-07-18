export default function DataAnalysisSessionPicker({
  sessions = [],
  selectedSessionId,
  onChange,
  hidden,
  onToggle,
  loading = false,
}) {
  const selected = sessions.find((item) => item.id === selectedSessionId) ?? null;
  const selectedLabel = selected?.label ?? selectedSessionId ?? "No session selected";

  if (hidden) {
    return (
      <section className="flex flex-wrap items-center justify-between gap-3 rounded-[1.5rem] border border-slate-800 bg-slate-900/65 px-4 py-3">
        <div className="min-w-0">
          <div className="text-[10px] font-semibold uppercase tracking-[0.24em] text-slate-500">
            Selected session
          </div>
          <div className="mt-1 truncate text-sm font-semibold text-white">{selectedLabel}</div>
        </div>
        <button
          type="button"
          onClick={onToggle}
          className="rounded-xl border border-cyan-400/35 bg-cyan-400/10 px-3 py-2 text-xs font-semibold text-cyan-100 transition hover:bg-cyan-400/20"
        >
          Show session switcher
        </button>
      </section>
    );
  }

  return (
    <section className="rounded-[1.75rem] border border-cyan-400/20 bg-[linear-gradient(135deg,rgba(8,47,73,0.55),rgba(15,23,42,0.84))] p-4 shadow-[0_18px_60px_rgba(2,6,23,0.24)]">
      <div className="flex flex-col gap-4 lg:flex-row lg:items-end lg:justify-between">
        <div>
          <div className="text-[10px] font-semibold uppercase tracking-[0.3em] text-cyan-300">
            Real session data source
          </div>
          <h2 className="mt-1 text-xl font-semibold text-white">Selected greenhouse scan</h2>
          <p className="mt-1 max-w-3xl text-sm leading-6 text-slate-400">
            This selection is shared by the spatial model, Kriging, M5Stick, PCA, and data-quality tabs.
          </p>
        </div>

        <div className="flex flex-col gap-2 sm:flex-row sm:items-end">
          <label className="flex min-w-[290px] flex-col gap-2 text-xs font-semibold uppercase tracking-[0.16em] text-slate-400">
            Session
            <select
              value={selectedSessionId ?? ""}
              disabled={loading || sessions.length === 0}
              onChange={(event) => onChange(event.target.value)}
              className="rounded-xl border border-slate-700 bg-slate-950 px-3 py-2.5 text-sm font-medium normal-case tracking-normal text-white outline-none transition focus:border-cyan-400 disabled:cursor-not-allowed disabled:opacity-60"
            >
              {sessions.length === 0 ? (
                <option value="">No session-data folders found</option>
              ) : (
                sessions.map((session) => (
                  <option key={session.id} value={session.id}>
                    {session.label} — {session.id}
                  </option>
                ))
              )}
            </select>
          </label>

          <button
            type="button"
            onClick={onToggle}
            className="rounded-xl border border-slate-700 bg-slate-950/70 px-4 py-2.5 text-sm font-semibold text-slate-300 transition hover:border-slate-500 hover:text-white"
          >
            Hide
          </button>
        </div>
      </div>

      {selected && (
        <div className="mt-4 flex flex-wrap gap-2 text-xs text-slate-300">
          <span className="rounded-full border border-slate-700 bg-slate-950/70 px-3 py-1.5">
            {selected.counts?.detectionEvents ?? 0} detection events
          </span>
          <span className="rounded-full border border-slate-700 bg-slate-950/70 px-3 py-1.5">
            {selected.counts?.mapPoseRows ?? 0} pose rows
          </span>
          <span className="rounded-full border border-slate-700 bg-slate-950/70 px-3 py-1.5">
            {selected.counts?.timelineRows ?? 0} M5Stick rows
          </span>
        </div>
      )}
    </section>
  );
}
