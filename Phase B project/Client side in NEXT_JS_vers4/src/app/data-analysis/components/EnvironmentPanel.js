const METRICS = [
  { key: "tempC", label: "Temperature", unit: "°C", min: 10, max: 40, target: [18, 30] },
  { key: "humidityPct", label: "Humidity", unit: "%", min: 30, max: 90, target: [50, 75] },
  { key: "pressureHpa", label: "Pressure", unit: "hPa", min: 970, max: 1040, target: [990, 1025] },
  { key: "gasKohm", label: "Gas resistance", unit: "kΩ", min: 0, max: 500, target: [100, 300] },
];

function mean(values) {
  const clean = values.filter((v) => Number.isFinite(v));
  if (!clean.length) return null;
  return clean.reduce((a, b) => a + b, 0) / clean.length;
}

function sparklinePath(points, metric, width, height, min, max) {
  const clean = points.filter((p) => Number.isFinite(p[metric]));
  if (!clean.length) return "";
  return clean.map((p, i) => {
    const x = clean.length === 1 ? 0 : (i / (clean.length - 1)) * width;
    const y = height - ((p[metric] - min) / Math.max(0.001, max - min)) * height;
    return `${i === 0 ? "M" : "L"}${x.toFixed(1)},${Math.max(0, Math.min(height, y)).toFixed(1)}`;
  }).join(" ");
}

export default function EnvironmentPanel({ series }) {
  return (
    <section className="rounded-[2rem] border border-slate-800 bg-slate-900/65 p-5">
      <div className="text-[11px] font-semibold uppercase tracking-[0.3em] text-cyan-300">M5Stick environment</div>
      <h2 className="mt-2 text-xl font-semibold text-white">Microclimate layer</h2>
      <p className="mt-1 text-sm text-slate-400">Current supported live data: temperature, humidity, pressure, and gas resistance.</p>

      <div className="mt-5 grid gap-3 md:grid-cols-2 xl:grid-cols-4">
        {METRICS.map((metric) => {
          const values = series.map((row) => row[metric.key]).filter((v) => Number.isFinite(v));
          const avg = mean(values);
          const last = values.at(-1);
          const min = Math.min(metric.min, ...values);
          const max = Math.max(metric.max, ...values);
          const inTarget = last >= metric.target[0] && last <= metric.target[1];

          return (
            <div key={metric.key} className="rounded-3xl border border-slate-800 bg-slate-950/70 p-4">
              <div className="flex items-center justify-between gap-3">
                <div>
                  <div className="text-sm font-semibold text-white">{metric.label}</div>
                  <div className="mt-1 text-xs text-slate-500">target {metric.target[0]}–{metric.target[1]} {metric.unit}</div>
                </div>
                <span className={`rounded-full px-2.5 py-1 text-[10px] font-semibold uppercase tracking-[0.16em] ${inTarget ? "bg-emerald-400/15 text-emerald-200" : "bg-amber-400/15 text-amber-200"}`}>{inTarget ? "stable" : "watch"}</span>
              </div>

              <div className="mt-4 text-3xl font-semibold text-white">{last?.toFixed(1) ?? "—"}<span className="ml-1 text-sm text-slate-400">{metric.unit}</span></div>
              <div className="mt-1 text-xs text-slate-500">avg {avg?.toFixed(1) ?? "—"} {metric.unit}</div>

              <svg viewBox="0 0 220 82" className="mt-4 h-24 w-full overflow-visible">
                <rect x="0" y="0" width="220" height="82" rx="16" fill="rgba(15,23,42,0.8)" />
                <rect x="0" y={(82 - ((metric.target[1] - min) / (max - min)) * 82).toFixed(1)} width="220" height={(((metric.target[1] - metric.target[0]) / (max - min)) * 82).toFixed(1)} fill="rgba(16,185,129,0.13)" />
                <path d={sparklinePath(series, metric.key, 220, 82, min, max)} fill="none" stroke="rgba(56,189,248,0.95)" strokeWidth="3" strokeLinecap="round" />
              </svg>
            </div>
          );
        })}
      </div>
    </section>
  );
}
