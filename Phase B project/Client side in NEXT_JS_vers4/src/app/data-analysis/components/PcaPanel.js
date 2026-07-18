import { useState } from "react";
import { calculatePca, strongestLoadings } from "../lib/pcaModel";

function formatPercent(value) {
  return `${Math.round((value ?? 0) * 100)}%`;
}

function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}

const VARIABLE_COLORS = [
  "#67e8f9",
  "#f472b6",
  "#a78bfa",
  "#34d399",
  "#fbbf24",
  "#fb7185",
  "#60a5fa",
  "#f97316",
  "#c084fc",
  "#22c55e",
];

function variableColor(index) {
  return VARIABLE_COLORS[index % VARIABLE_COLORS.length];
}

function MetricCard({ label, value, detail }) {
  return (
    <div className="rounded-3xl border border-slate-800 bg-slate-950/70 p-4">
      <div className="text-[10px] font-semibold uppercase tracking-[0.22em] text-slate-500">{label}</div>
      <div className="mt-2 text-2xl font-semibold text-white">{value}</div>
      <div className="mt-1 min-h-[2.5rem] text-sm text-slate-400">{detail}</div>
    </div>
  );
}

function Biplot({ pca }) {
  const width = 920;
  const height = 560;
  const pad = 62;
  const scores = pca.scores;
  const loadings = pca.loadings;
  const [selectedVariable, setSelectedVariable] = useState(null);

  const maxScore = Math.max(
    1,
    ...scores.flatMap((score) => [Math.abs(score.pc1), Math.abs(score.pc2)]),
  );

  const maxLoading = Math.max(
    0.001,
    ...loadings.flatMap((loading) => [Math.abs(loading.pc1 ?? 0), Math.abs(loading.pc2 ?? 0)]),
  );

  const sx = (value) => width / 2 + (value / maxScore) * (width / 2 - pad);
  const sy = (value) => height / 2 - (value / maxScore) * (height / 2 - pad);
  const lx = (value) => width / 2 + (value / maxLoading) * (width / 2 - pad) * 0.82;
  const ly = (value) => height / 2 - (value / maxLoading) * (height / 2 - pad) * 0.82;

  const orderedLoadings = selectedVariable
    ? [
        ...loadings.filter((loading) => loading.key !== selectedVariable),
        ...loadings.filter((loading) => loading.key === selectedVariable),
      ]
    : loadings;

  return (
    <div className="overflow-hidden rounded-3xl border border-slate-800 bg-slate-950/80 p-4">
      <div className="mb-3 flex flex-wrap items-center justify-between gap-3">
        <div>
          <div className="text-sm font-semibold text-white">PC1 / PC2 biplot</div>
          <div className="mt-1 text-xs text-slate-400">
            Points are real selected-session tomato landmarks. Arrows show original variable loadings. Select a variable in the legend to highlight its arrow.
          </div>
        </div>
        <div className="text-xs text-slate-400">
          PC1 {formatPercent(pca.explainedVariance[0])} · PC2 {formatPercent(pca.explainedVariance[1])}
        </div>
      </div>

      <div className="mb-4 flex flex-wrap gap-2">
        {loadings.map((loading, index) => {
          const color = variableColor(index);
          const active = selectedVariable === loading.key;

          return (
            <button
              key={loading.key}
              type="button"
              onClick={() => setSelectedVariable(active ? null : loading.key)}
              className={`flex items-center gap-2 rounded-full border px-3 py-1.5 text-xs font-semibold transition ${
                active
                  ? "border-white bg-white text-slate-950"
                  : "border-slate-700 bg-slate-900/70 text-slate-300 hover:border-slate-500 hover:text-white"
              }`}
            >
              <span className="h-3 w-3 rounded-full" style={{ backgroundColor: color }} />
              {loading.label}
            </button>
          );
        })}
      </div>

      <svg viewBox={`0 0 ${width} ${height}`} className="h-[520px] w-full">
        <defs>
          {loadings.map((loading, index) => (
            <marker
              key={`marker-${loading.key}`}
              id={`pca-arrow-${loading.key}`}
              markerWidth="9"
              markerHeight="9"
              refX="7"
              refY="3"
              orient="auto"
              markerUnits="strokeWidth"
            >
              <path d="M0,0 L0,6 L8,3 z" fill={variableColor(index)} />
            </marker>
          ))}
        </defs>

        <rect width={width} height={height} rx="24" fill="#020617" />
        <line x1={pad} y1={height / 2} x2={width - pad} y2={height / 2} stroke="rgba(148,163,184,0.35)" strokeDasharray="6 8" />
        <line x1={width / 2} y1={pad} x2={width / 2} y2={height - pad} stroke="rgba(148,163,184,0.35)" strokeDasharray="6 8" />
        <text x={width - pad - 110} y={height / 2 - 12} fill="rgba(226,232,240,0.65)" fontSize="13">PC1</text>
        <text x={width / 2 + 12} y={pad + 18} fill="rgba(226,232,240,0.65)" fontSize="13">PC2</text>

        {scores.map((score) => {
          const maturity = clamp(score.maturityScore, 0, 1);
          const fill = maturity >= 0.75 ? "#fb7185" : maturity >= 0.42 ? "#fbbf24" : "#22c55e";

          return (
            <g key={score.id}>
              <circle cx={sx(score.pc1)} cy={sy(score.pc2)} r="8" fill={fill} stroke="rgba(255,255,255,0.55)" />
              <text x={sx(score.pc1) + 10} y={sy(score.pc2) - 8} fill="rgba(226,232,240,0.72)" fontSize="11">{score.id}</text>
            </g>
          );
        })}

        {orderedLoadings.map((loading) => {
          const originalIndex = loadings.findIndex((item) => item.key === loading.key);
          const color = variableColor(originalIndex);
          const active = selectedVariable === loading.key;
          const dimmed = selectedVariable && !active;
          const x2 = lx(loading.pc1 ?? 0);
          const y2 = ly(loading.pc2 ?? 0);

          return (
            <g key={loading.key}>
              <line
                x1={width / 2}
                y1={height / 2}
                x2={x2}
                y2={y2}
                stroke={color}
                strokeWidth={active ? 5 : 2.4}
                opacity={dimmed ? 0.18 : 0.95}
                markerEnd={`url(#pca-arrow-${loading.key})`}
              />

              {active && (
                <text
                  x={x2 + 10}
                  y={y2 - 8}
                  fill={color}
                  fontSize="14"
                  fontWeight="700"
                >
                  {loading.label}
                </text>
              )}
            </g>
          );
        })}
      </svg>

      <div className="mt-3 text-xs leading-5 text-slate-500">
        Clicking a variable highlights its loading arrow and draws it above the other arrows. Click the same variable again to clear the selection.
      </div>
    </div>
  );
}

function LoadingsTable({ pca }) {
  return (
    <div className="rounded-3xl border border-slate-800 bg-slate-950/70 p-4">
      <div className="text-sm font-semibold text-white">Variable loadings</div>
      <p className="mt-1 text-xs leading-5 text-slate-400">
        Large absolute values show which original variables contribute most strongly to each principal component.
      </p>

      <div className="mt-4 overflow-x-auto">
        <table className="min-w-full text-left text-sm">
          <thead className="text-[10px] uppercase tracking-[0.2em] text-slate-500">
            <tr>
              <th className="border-b border-slate-800 px-3 py-2">Variable</th>
              <th className="border-b border-slate-800 px-3 py-2">PC1 loading</th>
              <th className="border-b border-slate-800 px-3 py-2">PC2 loading</th>
              <th className="border-b border-slate-800 px-3 py-2">Unit</th>
            </tr>
          </thead>
          <tbody>
            {pca.loadings.map((loading) => (
              <tr key={loading.key} className="text-slate-300">
                <td className="border-b border-slate-900 px-3 py-2 font-semibold text-white">{loading.label}</td>
                <td className="border-b border-slate-900 px-3 py-2">{(loading.pc1 ?? 0).toFixed(3)}</td>
                <td className="border-b border-slate-900 px-3 py-2">{(loading.pc2 ?? 0).toFixed(3)}</td>
                <td className="border-b border-slate-900 px-3 py-2 text-slate-500">{loading.unit}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </div>
  );
}

function InterpretationBox({ pca }) {
  const pc1 = strongestLoadings(pca.loadings, "pc1", 3);
  const pc2 = strongestLoadings(pca.loadings, "pc2", 3);
  const pc1Interpretation = pca.componentInterpretations?.find((item) => item.id === "PC1");
  const pc2Interpretation = pca.componentInterpretations?.find((item) => item.id === "PC2");

  return (
    <div className="rounded-3xl border border-slate-800 bg-slate-950/70 p-4">
      <div className="text-sm font-semibold text-white">Interpretation for the model</div>

      <div className="mt-3 grid gap-4 lg:grid-cols-2">
        <div className="rounded-2xl border border-cyan-400/20 bg-cyan-400/10 p-3">
          <div className="text-xs font-semibold uppercase tracking-[0.18em] text-cyan-300">
            PC1 automatic label
          </div>
          <div className="mt-2 text-lg font-semibold text-white">
            {pc1Interpretation?.label ?? "Mixed PCA gradient"}
          </div>
          <p className="mt-2 text-sm leading-6 text-slate-300">
            {pc1Interpretation?.explanation ?? "This component is interpreted from the current loading values."}
          </p>
        </div>

        <div className="rounded-2xl border border-fuchsia-400/20 bg-fuchsia-400/10 p-3">
          <div className="text-xs font-semibold uppercase tracking-[0.18em] text-fuchsia-300">
            PC2 automatic label
          </div>
          <div className="mt-2 text-lg font-semibold text-white">
            {pc2Interpretation?.label ?? "Mixed PCA gradient"}
          </div>
          <p className="mt-2 text-sm leading-6 text-slate-300">
            {pc2Interpretation?.explanation ?? "This component is interpreted from the current loading values."}
          </p>
        </div>
      </div>

      <div className="mt-4 grid gap-4 lg:grid-cols-2">
        <div className="rounded-2xl border border-slate-800 bg-slate-900/70 p-3">
          <div className="text-xs font-semibold uppercase tracking-[0.18em] text-cyan-300">
            PC1 strongest variables
          </div>
          <ul className="mt-2 space-y-1 text-sm text-slate-300">
            {pc1.map((item) => (
              <li key={item.key}>• {item.label}: {(item.pc1 ?? 0).toFixed(3)}</li>
            ))}
          </ul>
        </div>

        <div className="rounded-2xl border border-slate-800 bg-slate-900/70 p-3">
          <div className="text-xs font-semibold uppercase tracking-[0.18em] text-fuchsia-300">
            PC2 strongest variables
          </div>
          <ul className="mt-2 space-y-1 text-sm text-slate-300">
            {pc2.map((item) => (
              <li key={item.key}>• {item.label}: {(item.pc2 ?? 0).toFixed(3)}</li>
            ))}
          </ul>
        </div>
      </div>

      <p className="mt-4 text-sm leading-6 text-slate-400">
        PCA is exploratory: PC1 and PC2 are interpreted from the loadings after calculation.
        The automatic labels are generated from the strongest loading groups in the current data layer and may change when the selected session or timeline position changes.
      </p>
    </div>
  );
}

export default function PcaPanel({ envSeries, tomatoSamples }) {
  const pca = calculatePca(envSeries, tomatoSamples, 3);
  const pc1Label = pca.componentInterpretations?.find((item) => item.id === "PC1")?.label;
  const pc2Label = pca.componentInterpretations?.find((item) => item.id === "PC2")?.label;

  if (!pca.ready) {
    return (
      <section className="rounded-[2rem] border border-slate-800 bg-slate-900/65 p-5">
        <div className="text-[11px] font-semibold uppercase tracking-[0.3em] text-cyan-300">PCA analysis</div>
        <h2 className="mt-2 text-2xl font-semibold text-white">Principal Component Analysis</h2>
        <p className="mt-3 text-sm leading-6 text-slate-400">{pca.reason}</p>
      </section>
    );
  }

  return (
    <section className="space-y-5 rounded-[2rem] border border-slate-800 bg-slate-900/65 p-5">
      <div>
        <div className="text-[11px] font-semibold uppercase tracking-[0.3em] text-cyan-300">PCA analysis</div>
        <h2 className="mt-2 text-2xl font-semibold text-white">Principal Component Analysis for EcoFarm variables</h2>
        <p className="mt-2 max-w-4xl text-sm leading-6 text-slate-400">
          PCA standardizes the selected environmental, spatial, and tomato variables, computes principal components, and shows which variables explain most of the variation in the current selected-session data.
        </p>
      </div>

      <div className="grid gap-3 md:grid-cols-2 xl:grid-cols-4">
        <MetricCard label="Observations" value={pca.rows.length} detail="Real selected-session tomato landmarks used in PCA." />
        <MetricCard label="Variables" value={pca.variables.length} detail="Standardized input variables." />
        <MetricCard label="PC1 variance" value={formatPercent(pca.explainedVariance[0])} detail={pc1Label ? `Main axis: ${pc1Label}` : "Largest direction of variation."} />
        <MetricCard label="PC1 + PC2" value={formatPercent(pca.cumulativeVariance[1])} detail={pc2Label ? `PC2 axis: ${pc2Label}` : "Cumulative explained variance."} />
      </div>

      <Biplot pca={pca} />

      <div className="grid gap-5 xl:grid-cols-[1.15fr_0.85fr]">
        <LoadingsTable pca={pca} />
        <InterpretationBox pca={pca} />
      </div>
    </section>
  );
}
