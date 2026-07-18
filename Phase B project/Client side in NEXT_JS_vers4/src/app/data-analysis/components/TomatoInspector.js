function Stat({ label, value }) {
  return (
    <div className="rounded-2xl border border-slate-800 bg-slate-950/70 p-3">
      <div className="text-[10px] font-semibold uppercase tracking-[0.22em] text-slate-500">{label}</div>
      <div className="mt-1 text-lg font-semibold text-white">{value}</div>
    </div>
  );
}

export default function TomatoInspector({ selected, samples, spatialSummary }) {
  const ripe = samples.filter((s) => s.maturityScore >= 0.75).length;
  const mixed = samples.filter((s) => s.maturityScore > 0.25 && s.maturityScore < 0.75).length;
  const green = samples.filter((s) => s.maturityScore <= 0.25).length;

  return (
    <aside className="rounded-[2rem] border border-slate-800 bg-slate-900/65 p-5">
      <div className="text-[11px] font-semibold uppercase tracking-[0.3em] text-emerald-300">Tomato layer</div>
      <h2 className="mt-2 text-xl font-semibold text-white">Cluster inspector</h2>

      {selected ? (
        <div className="mt-5 space-y-4">
          <div className="rounded-3xl border border-slate-800 bg-slate-950/70 p-4">
            <div className="flex items-start justify-between gap-3">
              <div>
                <div className="text-2xl font-semibold text-white">{selected.id}</div>
                <div className="mt-1 text-sm text-slate-400">{selected.label}</div>
                <div className="text-sm text-slate-500">{selected.hebrewLabel}</div>
              </div>
              <span className="h-5 w-5 rounded-full border border-white/30" style={{ background: selected.color }} />
            </div>
          </div>

          <div className="grid grid-cols-2 gap-3">
            <Stat label="Maturity" value={`${Math.round(selected.maturityScore * 100)}%`} />
            <Stat label="Confidence" value={`${Math.round((selected.confidence ?? 0) * 100)}%`} />
            <Stat label="Count" value={selected.count} />
            <Stat label="Position" value={`${selected.x.toFixed(1)}, ${selected.y.toFixed(1)}m`} />
          </div>
        </div>
      ) : (
        <div className="mt-5 rounded-3xl border border-slate-800 bg-slate-950/70 p-4 text-sm text-slate-400">
          Select a tomato point on the map to inspect class, confidence, maturity score, and spatial position.
        </div>
      )}

      <div className="mt-5 grid grid-cols-3 gap-3">
        <Stat label="Ripe" value={ripe} />
        <Stat label="Mixed" value={mixed} />
        <Stat label="Green" value={green} />
      </div>

      <div className="mt-5 rounded-3xl border border-slate-800 bg-slate-950/70 p-4">
        <div className="text-[10px] font-semibold uppercase tracking-[0.22em] text-slate-500">Model coverage</div>
        <div className="mt-2 h-2 overflow-hidden rounded-full bg-slate-800">
          <div className="h-full rounded-full bg-cyan-400" style={{ width: `${spatialSummary.coverage}%` }} />
        </div>
        <div className="mt-2 text-sm text-slate-300">{spatialSummary.coverage.toFixed(0)}% prototype scan coverage</div>
      </div>
    </aside>
  );
}
