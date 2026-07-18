function MetricCard({ label, value, detail }) {
  return (
    <div className="rounded-3xl border border-slate-800 bg-slate-950/70 p-4">
      <div className="text-[10px] font-semibold uppercase tracking-[0.22em] text-slate-500">{label}</div>
      <div className="mt-2 text-2xl font-semibold text-white">{value}</div>
      <div className="mt-1 min-h-[2.5rem] text-sm text-slate-400">{detail}</div>
    </div>
  );
}


export default function SpatialModelPanel({ summary }) {
  return (
    <section className="rounded-[2rem] border border-slate-800 bg-slate-900/65 p-5">
      <div className="text-[11px] font-semibold uppercase tracking-[0.3em] text-fuchsia-300">Spatial statistics</div>
      <h2 className="mt-2 text-xl font-semibold text-white">Autocorrelation and Kriging readiness</h2>
      <p className="mt-1 text-sm text-slate-400">These values are prototype indicators. Replace the mock YOLO12M log with real detections when available.</p>

      <div className="mt-5 grid gap-3 md:grid-cols-2 xl:grid-cols-5">
        <MetricCard label="Moran's I" value={summary.moran.value.toFixed(2)} detail={summary.moran.label} />
        <MetricCard label="Geary's C" value={summary.geary.value.toFixed(2)} detail={summary.geary.label} />
        <MetricCard label="Variogram range" value={`${summary.variogram.rangeMeters.toFixed(1)}m`} detail={`Model: ${summary.variogram.model}, sill ${summary.variogram.sill.toFixed(2)}`} />
        <MetricCard label="Avg uncertainty" value={`${Math.round(summary.uncertaintyAverage * 100)}%`} detail="Lower uncertainty means the robot sampled nearby points." />
        <MetricCard label="Moran p-value" value={summary.moranTest?.pValue != null ? summary.moranTest.pValue.toFixed(3) : "—"} detail="Permutation test for spatial autocorrelation" />
      </div>

      <div className="mt-5 rounded-3xl border border-slate-800 bg-slate-950/70 p-4">
        <div className="text-sm font-semibold text-white">Interpretation for the course</div>
        <p className="mt-2 text-sm leading-6 text-slate-400">
          If nearby tomato clusters have similar maturity, the map should show positive spatial autocorrelation. Kriging then uses that spatial structure to estimate maturity in unsampled greenhouse cells and to show where prediction uncertainty is high.
        </p>
      </div>
    </section>
  );
}
