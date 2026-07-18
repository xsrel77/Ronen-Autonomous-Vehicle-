// src/app/data-analysis/components/MicroclimatePanel.js
"use client";

import { useEffect, useMemo, useRef, useState } from "react";

const METRICS = {
  tempC: {
    label: "Temperature",
    short: "Temp",
    unit: "°C",
    color: "#fb7185",
    soft: "rgba(251,113,133,0.14)",
    digits: 1,
    displayRange: [10, 40],
    thresholds: [
      { from: 10, to: 18, label: "cold", color: "rgba(59,130,246,0.12)" },
      { from: 18, to: 26, label: "target", color: "rgba(16,185,129,0.12)" },
      { from: 26, to: 30, label: "warm", color: "rgba(234,179,8,0.10)" },
      { from: 30, to: 35, label: "heat", color: "rgba(249,115,22,0.13)" },
      { from: 35, to: 40, label: "severe heat", color: "rgba(239,68,68,0.14)" },
    ],
  },
  humidityPct: {
    label: "Humidity",
    short: "Humidity",
    unit: "%",
    color: "#38bdf8",
    soft: "rgba(56,189,248,0.14)",
    digits: 1,
    displayRange: [20, 100],
    thresholds: [
      { from: 20, to: 40, label: "dry", color: "rgba(234,179,8,0.13)" },
      { from: 40, to: 50, label: "low", color: "rgba(59,130,246,0.08)" },
      { from: 50, to: 70, label: "target", color: "rgba(16,185,129,0.12)" },
      { from: 70, to: 80, label: "humid", color: "rgba(6,182,212,0.11)" },
      { from: 80, to: 100, label: "disease risk", color: "rgba(245,158,11,0.14)" },
    ],
  },
  pressureHpa: {
    label: "Pressure",
    short: "Pressure",
    unit: "hPa",
    color: "#a3e635",
    soft: "rgba(163,230,53,0.14)",
    digits: 1,
    displayRange: [970, 1040],
    thresholds: [
      { from: 970, to: 990, label: "low pressure", color: "rgba(59,130,246,0.10)" },
      { from: 990, to: 1025, label: "normal", color: "rgba(16,185,129,0.10)" },
      { from: 1025, to: 1040, label: "high pressure", color: "rgba(234,179,8,0.10)" },
    ],
  },
  gasKohm: {
    label: "Gas Resistance",
    short: "Gas",
    unit: "kΩ",
    color: "#f59e0b",
    soft: "rgba(245,158,11,0.14)",
    digits: 1,
    displayRange: [0, 500],
    thresholds: [
      { from: 0, to: 50, label: "low air quality", color: "rgba(239,68,68,0.14)" },
      { from: 50, to: 100, label: "watch", color: "rgba(245,158,11,0.13)" },
      { from: 100, to: 300, label: "normal", color: "rgba(16,185,129,0.10)" },
      { from: 300, to: 500, label: "high resistance", color: "rgba(59,130,246,0.09)" },
    ],
  },
};

const METRIC_KEYS = Object.keys(METRICS);

const FORECAST_STEPS = 5;
const REGRESSION_WINDOW_POINTS = 42;

const TIME_MODES = {
  seconds: {
    label: "Seconds",
    stepSec: 1,
    axisUnit: "s",
    bucketLabel: "1-second averages",
    futureLabel: "+1s",
  },
  minutes: {
    label: "Minutes",
    stepSec: 60,
    axisUnit: "m",
    bucketLabel: "1-minute averages",
    futureLabel: "+1m",
  },
  hours: {
    label: "Hours",
    stepSec: 3600,
    axisUnit: "h",
    bucketLabel: "1-hour averages",
    futureLabel: "+1h",
  },
};

const Y_SCALE_MODES = {
  context: {
    label: "Farm Range",
    description: "Shows real-life farm thresholds and expected environmental ranges.",
  },
  data: {
    label: "Data Range",
    description: "Zooms the Y-axis to the min/max of the values in this run.",
  },
};

function num(value) {
  if (value == null || value === "") return null;
  const n = Number(value);
  return Number.isFinite(n) ? n : null;
}

function fmt(value, digits = 1, fallback = "—") {
  const n = num(value);
  return n === null ? fallback : n.toFixed(digits);
}

function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}

function mean(values) {
  const arr = values.map(num).filter((v) => v !== null);
  if (!arr.length) return null;
  return arr.reduce((a, b) => a + b, 0) / arr.length;
}

function std(values) {
  const arr = values.map(num).filter((v) => v !== null);
  if (arr.length < 2) return 0;

  const avg = mean(arr);
  const variance =
    arr.reduce((sum, v) => sum + (v - avg) ** 2, 0) / (arr.length - 1);

  return Math.sqrt(variance);
}

function minMax(values) {
  const arr = values.map(num).filter((v) => v !== null);
  if (!arr.length) return { min: null, max: null };
  return { min: Math.min(...arr), max: Math.max(...arr) };
}

function compactTime(sec) {
  const s = Math.max(0, Math.round(num(sec) ?? 0));

  if (s < 60) return `${s}s`;

  if (s < 3600) {
    const m = Math.floor(s / 60);
    const r = s % 60;
    return `${m}:${String(r).padStart(2, "0")}`;
  }

  if (s < 86400) {
    const h = Math.floor(s / 3600);
    const m = Math.floor((s % 3600) / 60);
    return `${h}h ${m}m`;
  }

  const d = Math.floor(s / 86400);
  const h = Math.floor((s % 86400) / 3600);
  return `${d}d ${h}h`;
}

function formatStepTime(stepIndex, timeMode) {
  const mode = TIME_MODES[timeMode];

  if (timeMode === "seconds") return `${stepIndex}s`;
  if (timeMode === "minutes") return `${stepIndex}m`;
  if (timeMode === "hours") return `${stepIndex}h`;

  return `${stepIndex}${mode.axisUnit}`;
}

function normalizeSeries(rawSeries = []) {
  const normalized = rawSeries
    .map((item, index) => {
      const rawTime =
        num(
          item.timestampMs ??
            item.timestamp_ms ??
            item.timeMs ??
            item.tMs ??
            item.t_ms ??
            item.tSec ??
            item.t_sec ??
            item.t,
        ) ?? index;

      const timestampMs =
        num(item.timestampMs ?? item.timestamp_ms ?? item.timeMs ?? item.tMs ?? item.t_ms) ??
        null;

      return {
        i: index,
        rawTime,
        timestampMs,
        tempC: num(item.tempC ?? item.temp_c ?? item.temperature),
        humidityPct: num(item.humidityPct ?? item.humidity_pct ?? item.humidity),
        pressureHpa: num(item.pressureHpa ?? item.pressure_hpa ?? item.pressure),
        gasKohm: num(item.gasKohm ?? item.gas_kohm ?? item.gas),
      };
    })
    .filter(
      (p) =>
        p.tempC !== null ||
        p.humidityPct !== null ||
        p.pressureHpa !== null ||
        p.gasKohm !== null,
    );

  if (!normalized.length) return [];

  const firstTimestamp = normalized.find((p) => p.timestampMs !== null)?.timestampMs ?? null;
  const firstRawTime = normalized[0].rawTime;

  return normalized.map((p, index) => {
    let tSec;

    if (p.timestampMs !== null && firstTimestamp !== null) {
      tSec = (p.timestampMs - firstTimestamp) / 1000;
    } else {
      const looksLikeMs = firstRawTime > 100000;
      tSec = looksLikeMs ? (p.rawTime - firstRawTime) / 1000 : p.rawTime - firstRawTime;
    }

    return {
      ...p,
      i: index,
      tSec: Math.max(0, tSec),
    };
  });
}

function aggregateSeriesByTimeMode(series, timeMode) {
  const mode = TIME_MODES[timeMode];
  const stepSec = mode.stepSec;

  if (!series.length) return [];

  const lastSec = series[series.length - 1]?.tSec ?? 0;
  const lastRealStep = Math.max(0, Math.floor(lastSec / stepSec));
  const finalStep = lastRealStep + FORECAST_STEPS;

  const buckets = new Map();

  for (const sample of series) {
    const step = Math.floor((sample.tSec ?? 0) / stepSec);

    if (!buckets.has(step)) {
      buckets.set(step, {
        step,
        startSec: step * stepSec,
        endSec: (step + 1) * stepSec,
        sampleCount: 0,
        tempC: [],
        humidityPct: [],
        pressureHpa: [],
        gasKohm: [],
      });
    }

    const bucket = buckets.get(step);
    bucket.sampleCount += 1;

    for (const key of METRIC_KEYS) {
      if (sample[key] !== null) bucket[key].push(sample[key]);
    }
  }

  const result = [];

  for (let step = 0; step <= finalStep; step += 1) {
    const bucket = buckets.get(step);

    if (!bucket) {
      result.push({
        step,
        startSec: step * stepSec,
        endSec: (step + 1) * stepSec,
        tSec: step * stepSec,
        sampleCount: 0,
        isEmpty: true,
        isPrediction: step > lastRealStep,
        tempC: null,
        humidityPct: null,
        pressureHpa: null,
        gasKohm: null,
      });
      continue;
    }

    result.push({
      step,
      startSec: bucket.startSec,
      endSec: bucket.endSec,
      tSec: bucket.startSec,
      sampleCount: bucket.sampleCount,
      isEmpty: false,
      isPrediction: false,
      tempC: mean(bucket.tempC),
      humidityPct: mean(bucket.humidityPct),
      pressureHpa: mean(bucket.pressureHpa),
      gasKohm: mean(bucket.gasKohm),
    });
  }

  return result;
}

function metricStats(series, key) {
  const values = series.map((p) => p[key]).filter((v) => v !== null);
  const mm = minMax(values);
  const current = values.length ? values[values.length - 1] : null;
  const first = values.length ? values[0] : null;
  const avg = mean(values);
  const sd = std(values);

  return {
    current,
    first,
    delta: current !== null && first !== null ? current - first : null,
    avg,
    std: sd,
    min: mm.min,
    max: mm.max,
    count: values.length,
  };
}

function sparklinePoints(values, width = 130, height = 44, pad = 4) {
  const valid = values.map(num).filter((v) => v !== null);
  if (!valid.length) return "";

  const min = Math.min(...valid);
  const max = Math.max(...valid);
  const range = max - min || 1;

  return valid
    .map((v, i) => {
      const x = pad + (i / Math.max(valid.length - 1, 1)) * (width - pad * 2);
      const y = height - pad - ((v - min) / range) * (height - pad * 2);
      return `${x},${y}`;
    })
    .join(" ");
}

function computeRegression(points) {
  if (points.length < 2) return null;

  const xs = points.map((p) => p.x);
  const ys = points.map((p) => p.y);
  const mx = mean(xs);
  const my = mean(ys);

  let numerator = 0;
  let denominator = 0;
  let sst = 0;
  let sse = 0;

  for (let i = 0; i < points.length; i += 1) {
    const dx = xs[i] - mx;
    numerator += dx * (ys[i] - my);
    denominator += dx * dx;
  }

  const slope = denominator === 0 ? 0 : numerator / denominator;
  const intercept = my - slope * mx;

  for (let i = 0; i < points.length; i += 1) {
    const pred = intercept + slope * xs[i];
    sst += (ys[i] - my) ** 2;
    sse += (ys[i] - pred) ** 2;
  }

  const r2 = sst === 0 ? 1 : clamp(1 - sse / sst, 0, 1);

  return { slope, intercept, r2 };
}

function forecastMetricFromAggregated(aggregatedSeries, key, timeMode) {
  const stepSec = TIME_MODES[timeMode].stepSec;

  const actual = aggregatedSeries
    .filter((p) => !p.isPrediction && p[key] !== null)
    .map((p) => ({ x: p.step, sec: p.tSec, y: p[key] }));

  if (!actual.length) {
    return {
      predicted: null,
      trend: "flat",
      confidence: 0,
      current: null,
      delta: null,
      slope: 0,
      stepSec,
      points: [],
    };
  }

  const lastActual = actual[actual.length - 1];
  const windowed = actual.slice(-REGRESSION_WINDOW_POINTS);

  if (windowed.length < 2) {
    const points = Array.from({ length: FORECAST_STEPS }, (_, idx) => {
      const step = lastActual.x + idx + 1;
      return {
        step,
        tSec: step * stepSec,
        y: lastActual.y,
        delta: 0,
        confidence: 0,
      };
    });

    return {
      predicted: lastActual.y,
      trend: "flat",
      confidence: 0,
      current: lastActual.y,
      delta: 0,
      slope: 0,
      stepSec,
      points,
    };
  }

  const reg = computeRegression(windowed);
  const points = Array.from({ length: FORECAST_STEPS }, (_, idx) => {
    const step = lastActual.x + idx + 1;
    const y = reg.intercept + reg.slope * step;

    return {
      step,
      tSec: step * stepSec,
      y,
      delta: y - lastActual.y,
      confidence: reg.r2,
    };
  });

  const finalPoint = points[points.length - 1];
  const delta = finalPoint.y - lastActual.y;

  let trend = "flat";
  if (delta > 0.15) trend = "up";
  if (delta < -0.15) trend = "down";

  return {
    predicted: finalPoint.y,
    trend,
    confidence: reg.r2,
    current: lastActual.y,
    delta,
    slope: reg.slope,
    stepSec,
    points,
  };
}

function corr(x, y) {
  if (x.length !== y.length || x.length < 2) return null;

  const mx = mean(x);
  const my = mean(y);

  let cov = 0;
  let vx = 0;
  let vy = 0;

  for (let i = 0; i < x.length; i += 1) {
    const dx = x[i] - mx;
    const dy = y[i] - my;
    cov += dx * dy;
    vx += dx * dx;
    vy += dy * dy;
  }

  if (vx === 0 || vy === 0) return null;
  return cov / Math.sqrt(vx * vy);
}

function computeCorrelations(series) {
  const map = {};

  for (const a of METRIC_KEYS) {
    map[a] = {};

    for (const b of METRIC_KEYS) {
      if (a === b) {
        map[a][b] = 1;
        continue;
      }

      const pairs = series
        .filter((p) => p[a] !== null && p[b] !== null)
        .map((p) => [p[a], p[b]]);

      map[a][b] = corr(
        pairs.map((p) => p[0]),
        pairs.map((p) => p[1]),
      );
    }
  }

  return map;
}

function classifyZone(sample) {
  const t = sample.tempC;
  const h = sample.humidityPct;

  if (t === null || h === null) return "missing";
  if (t > 35 && h < 40) return "severeHeatDry";
  if (t > 30 && h < 45) return "heatDry";
  if (t >= 24 && h >= 80) return "humidDisease";
  if (t > 35) return "severeHeat";
  if (t > 30) return "heat";
  if (t < 10) return "cold";
  if (h < 40) return "dry";
  if (h > 80) return "humid";
  if (t >= 18 && t <= 26 && h >= 50 && h <= 70) return "optimal";

  return "watch";
}

const ZONE_META = {
  optimal: { label: "Optimal", color: "#10b981" },
  watch: { label: "Watch", color: "#94a3b8" },
  cold: { label: "Cold", color: "#60a5fa" },
  heat: { label: "Heat", color: "#f97316" },
  severeHeat: { label: "Severe Heat", color: "#ef4444" },
  dry: { label: "Dry", color: "#eab308" },
  humid: { label: "Humid", color: "#06b6d4" },
  heatDry: { label: "Heat+Dry", color: "#fb7185" },
  severeHeatDry: { label: "Severe Heat+Dry", color: "#dc2626" },
  humidDisease: { label: "Humid+Warm", color: "#f59e0b" },
  missing: { label: "Missing", color: "#475569" },
};

function buildZoneDistribution(series) {
  const counts = Object.fromEntries(Object.keys(ZONE_META).map((k) => [k, 0]));

  for (const sample of series) {
    counts[classifyZone(sample)] += 1;
  }

  const total = Math.max(series.length, 1);

  return Object.entries(counts)
    .map(([key, count]) => ({
      key,
      label: ZONE_META[key].label,
      color: ZONE_META[key].color,
      count,
      pct: count / total,
    }))
    .filter((z) => z.count > 0)
    .sort((a, b) => b.count - a.count);
}

function trendArrow(trend) {
  if (trend === "up") return "↗";
  if (trend === "down") return "↘";
  return "→";
}

function trendText(trend) {
  if (trend === "up") return "Rising";
  if (trend === "down") return "Falling";
  return "Stable";
}

function qualityTone(value) {
  if (value >= 0.9) return "text-emerald-300";
  if (value >= 0.7) return "text-amber-300";
  return "text-rose-300";
}

function zoneRiskScore(zones) {
  const risky = zones
    .filter((z) =>
      [
        "cold",
        "heat",
        "severeHeat",
        "dry",
        "humid",
        "heatDry",
        "severeHeatDry",
        "humidDisease",
      ].includes(z.key),
    )
    .reduce((sum, z) => sum + z.pct, 0);

  return risky * 100;
}

function stabilityScore(statsMap, envValidity) {
  const tempPenalty = clamp((statsMap.tempC.std ?? 0) * 6, 0, 28);
  const humidityPenalty = clamp((statsMap.humidityPct.std ?? 0) * 1.2, 0, 28);

  const gasCv =
    statsMap.gasKohm.avg && statsMap.gasKohm.avg !== 0
      ? Math.abs((statsMap.gasKohm.std ?? 0) / statsMap.gasKohm.avg) * 100
      : 0;

  const gasPenalty = clamp(gasCv, 0, 24);
  const validityPenalty = clamp((1 - envValidity) * 100, 0, 30);

  return clamp(
    100 - tempPenalty - humidityPenalty - gasPenalty - validityPenalty,
    0,
    100,
  );
}

function describePrediction(metricKey, forecast, timeMode) {
  const meta = METRICS[metricKey];
  const mode = TIME_MODES[timeMode];

  if (forecast.predicted === null) {
    return "Not enough data for prediction.";
  }

  const delta = fmt(Math.abs(forecast.delta), meta.digits);

  return `${trendText(forecast.trend)}: ${fmt(forecast.current, meta.digits)} → ${fmt(
    forecast.predicted,
    meta.digits,
  )} ${meta.unit} after ${FORECAST_STEPS} ${mode.axisUnit}-steps. Δ ${delta} ${
    meta.unit
  }. Fit ${(forecast.confidence * 100).toFixed(0)}%.`;
}

function computeYDomain(metricKey, stats, forecast, aggregatedSeries, yScaleMode) {
  const meta = METRICS[metricKey];

  const actualValues = aggregatedSeries
    .filter((p) => !p.isPrediction && p[metricKey] !== null)
    .map((p) => p[metricKey]);

  const forecastValues = forecast.points?.map((p) => p.y) ?? [];

  const dataValues = [
    ...actualValues,
    ...forecastValues,
    stats.min,
    stats.max,
    stats.avg,
    stats.current,
    forecast.predicted,
  ].filter((v) => v !== null && Number.isFinite(Number(v)));

  let values;

  if (yScaleMode === "data") {
    values = dataValues;
  } else {
    values = [
      ...dataValues,
      ...(meta.displayRange ?? []),
      ...(meta.thresholds ?? []).flatMap((t) => [t.from, t.to]),
    ];
  }

  if (!values.length) return [0, 1];

  let yMin = Math.min(...values);
  let yMax = Math.max(...values);

  if (yMin === yMax) {
    const padding = Math.max(Math.abs(yMin) * 0.05, 1);
    return [yMin - padding, yMax + padding];
  }

  const range = yMax - yMin;
  const paddingRatio = yScaleMode === "data" ? 0.16 : 0.06;

  yMin -= range * paddingRatio;
  yMax += range * paddingRatio;

  return [yMin, yMax];
}

function chartWidthForSteps(stepCount, timeMode) {
  const base = 920;

  if (timeMode === "seconds") return Math.max(base, stepCount * 34);
  if (timeMode === "minutes") return Math.max(base, stepCount * 90);
  if (timeMode === "hours") return Math.max(base, stepCount * 150);

  return base;
}

function TinyKpi({ label, value, tone = "text-white" }) {
  return (
    <div className="rounded-2xl border border-slate-800 bg-slate-950/70 px-3 py-2">
      <div className="text-[10px] uppercase tracking-[0.24em] text-slate-500">
        {label}
      </div>
      <div className={`mt-1 text-lg font-semibold ${tone}`}>{value}</div>
    </div>
  );
}

function MetricButton({ metricKey, series, stats, forecast, selected, onClick }) {
  const meta = METRICS[metricKey];
  const values = series.map((p) => p[metricKey]).filter((v) => v !== null);
  const spark = sparklinePoints(values);

  return (
    <button
      type="button"
      onClick={() => onClick(metricKey)}
      className={`rounded-3xl border p-4 text-left transition ${
        selected
          ? "border-slate-600 bg-slate-900/95 shadow-lg shadow-slate-950/30"
          : "border-slate-800 bg-slate-900/60 hover:border-slate-700 hover:bg-slate-900/80"
      }`}
    >
      <div className="flex items-start justify-between gap-3">
        <div>
          <div className="text-[11px] uppercase tracking-[0.24em] text-slate-500">
            {meta.label}
          </div>

          <div className="mt-2 flex items-end gap-2">
            <div className="text-3xl font-semibold text-white">
              {fmt(stats.current, meta.digits)}
            </div>
            <div className="pb-1 text-sm text-slate-400">{meta.unit}</div>
          </div>
        </div>

        <div
          className="rounded-full px-2.5 py-1 text-sm font-semibold"
          style={{
            color: meta.color,
            backgroundColor: meta.soft,
          }}
        >
          {trendArrow(forecast.trend)} {fmt(forecast.predicted, meta.digits)}
        </div>
      </div>

      <div className="mt-3 flex items-center justify-between text-xs text-slate-400">
        <span>min {fmt(stats.min, meta.digits)}</span>
        <span>avg {fmt(stats.avg, meta.digits)}</span>
        <span>max {fmt(stats.max, meta.digits)}</span>
      </div>

      <svg viewBox="0 0 130 44" className="mt-3 h-11 w-full">
        <polyline
          fill="none"
          stroke={meta.color}
          strokeWidth="2.5"
          strokeLinecap="round"
          strokeLinejoin="round"
          points={spark}
        />
      </svg>
    </button>
  );
}

function TimeModeControl({ value, onChange }) {
  return (
    <div className="flex flex-wrap gap-2">
      {Object.entries(TIME_MODES).map(([key, item]) => (
        <button
          key={key}
          type="button"
          onClick={() => onChange(key)}
          className={`rounded-full border px-3 py-1.5 text-xs font-semibold uppercase tracking-[0.18em] transition ${
            value === key
              ? "border-emerald-400/50 bg-emerald-400/10 text-emerald-200"
              : "border-slate-700 bg-slate-950/50 text-slate-400 hover:border-slate-500 hover:text-slate-200"
          }`}
        >
          {item.label}
        </button>
      ))}
    </div>
  );
}

function YScaleControl({ value, onChange }) {
  return (
    <div className="flex flex-wrap gap-2">
      {Object.entries(Y_SCALE_MODES).map(([key, item]) => (
        <button
          key={key}
          type="button"
          onClick={() => onChange(key)}
          title={item.description}
          className={`rounded-full border px-3 py-1.5 text-xs font-semibold uppercase tracking-[0.18em] transition ${
            value === key
              ? "border-sky-400/50 bg-sky-400/10 text-sky-200"
              : "border-slate-700 bg-slate-950/50 text-slate-400 hover:border-slate-500 hover:text-slate-200"
          }`}
        >
          {item.label}
        </button>
      ))}
    </div>
  );
}

function MainChart({
  rawSeries,
  aggregatedSeries,
  metricKey,
  stats,
  forecast,
  timeMode,
  setTimeMode,
  yScaleMode,
  setYScaleMode,
}) {
  const chartRef = useRef(null);
  const [hoverPoint, setHoverPoint] = useState(null);

  const meta = METRICS[metricKey];
  const mode = TIME_MODES[timeMode];

  const actualPoints = aggregatedSeries
    .filter((p) => !p.isPrediction && !p.isEmpty && p[metricKey] !== null)
    .map((p) => ({
      step: p.step,
      x: p.step,
      tSec: p.tSec,
      y: p[metricKey],
      sampleCount: p.sampleCount,
      isPrediction: false,
      isEmpty: false,
    }));

  const emptyPoints = aggregatedSeries
    .filter((p) => p.isEmpty && !p.isPrediction)
    .map((p) => ({
      step: p.step,
      x: p.step,
      tSec: p.tSec,
      y: null,
      sampleCount: 0,
      isPrediction: false,
      isEmpty: true,
    }));

  const predictionPoints = forecast.points.map((p) => ({
    step: p.step,
    x: p.step,
    tSec: p.tSec,
    y: p.y,
    sampleCount: 0,
    isPrediction: true,
    isEmpty: false,
    delta: p.delta,
    confidence: p.confidence,
  }));

  if (!aggregatedSeries.length) return null;

  const lastActual = actualPoints[actualPoints.length - 1] ?? null;
  const minStep = 0;
  const maxStep = Math.max(
    aggregatedSeries[aggregatedSeries.length - 1]?.step ?? 0,
    predictionPoints[predictionPoints.length - 1]?.step ?? 0,
    FORECAST_STEPS,
  );

  const width = chartWidthForSteps(maxStep + 1, timeMode);
  const height = 430;
  const padL = 68;
  const padR = 58;
  const padT = 34;
  const padB = 64;

  const [yMin, yMax] = computeYDomain(
    metricKey,
    stats,
    forecast,
    aggregatedSeries,
    yScaleMode,
  );

  const yRange = yMax - yMin || 1;
  const xRange = maxStep - minStep || 1;

  function sx(step) {
    return padL + ((step - minStep) / xRange) * (width - padL - padR);
  }

  function sy(y) {
    return height - padB - ((y - yMin) / yRange) * (height - padT - padB);
  }

  const allHoverable = [...actualPoints, ...predictionPoints].filter((p) => p.y !== null);

  function handleMouseMove(event) {
    const rect = chartRef.current?.getBoundingClientRect();
    if (!rect || !allHoverable.length) return;

    const mouseX = ((event.clientX - rect.left) / rect.width) * width;

    let nearest = allHoverable[0];
    let nearestDist = Math.abs(sx(allHoverable[0].x) - mouseX);

    for (const point of allHoverable) {
      const dist = Math.abs(sx(point.x) - mouseX);

      if (dist < nearestDist) {
        nearest = point;
        nearestDist = dist;
      }
    }

    setHoverPoint(nearest);
  }

  const actualPolyline = actualPoints.map((p) => `${sx(p.x)},${sy(p.y)}`).join(" ");

  const predictionPolyline =
    lastActual && predictionPoints.length
      ? [
          `${sx(lastActual.x)},${sy(lastActual.y)}`,
          ...predictionPoints.map((p) => `${sx(p.x)},${sy(p.y)}`),
        ].join(" ")
      : "";

  const avgY = stats.avg !== null ? sy(stats.avg) : null;

  const gridYs = [0.2, 0.4, 0.6, 0.8].map(
    (t) => padT + t * (height - padT - padB),
  );

  const ticks = Array.from({ length: maxStep + 1 }, (_, step) => step);
  const visibleTicks = ticks.filter((step) => {
    if (timeMode === "seconds") return step % 10 === 0 || step === maxStep;
    return true;
  });

  const minPoint = actualPoints.reduce(
    (best, p) => (best === null || p.y < best.y ? p : best),
    null,
  );

  const maxPoint = actualPoints.reduce(
    (best, p) => (best === null || p.y > best.y ? p : best),
    null,
  );

  const hoverX = hoverPoint ? sx(hoverPoint.x) : null;
  const hoverY = hoverPoint ? sy(hoverPoint.y) : null;

  const tooltipX = hoverPoint ? clamp(hoverX + 14, padL + 8, width - 250) : 0;
  const tooltipY = hoverPoint ? clamp(hoverY - 70, padT + 8, height - padB - 116) : 0;

  const runDuration = compactTime(rawSeries[rawSeries.length - 1]?.tSec ?? 0);
  const predictionDescription = describePrediction(metricKey, forecast, timeMode);

  return (
    <div className="rounded-[2rem] border border-slate-800 bg-slate-900/65 p-5">
      <div className="flex flex-wrap items-start justify-between gap-4">
        <div>
          <div className="text-[11px] uppercase tracking-[0.28em] text-slate-500">
            Main View · {mode.bucketLabel}
          </div>

          <div className="mt-2 text-2xl font-semibold text-white">
            {meta.label}
          </div>

          <div className="mt-2 max-w-3xl text-sm text-slate-400">
            {predictionDescription}
          </div>
        </div>

        <div className="space-y-3">
          <div>
            <div className="mb-1 text-[10px] uppercase tracking-[0.22em] text-slate-500">
              Time scale
            </div>
            <TimeModeControl value={timeMode} onChange={setTimeMode} />
          </div>

          <div>
            <div className="mb-1 text-[10px] uppercase tracking-[0.22em] text-slate-500">
              Y-axis range
            </div>
            <YScaleControl value={yScaleMode} onChange={setYScaleMode} />
          </div>

          <div className="grid grid-cols-2 gap-2 sm:grid-cols-5">
            <TinyKpi label="Min" value={`${fmt(stats.min, meta.digits)} ${meta.unit}`} />
            <TinyKpi label="Avg" value={`${fmt(stats.avg, meta.digits)} ${meta.unit}`} />
            <TinyKpi label="Max" value={`${fmt(stats.max, meta.digits)} ${meta.unit}`} />
            <TinyKpi label="Now" value={`${fmt(stats.current, meta.digits)} ${meta.unit}`} />
            <TinyKpi
              label={`+${FORECAST_STEPS}${mode.axisUnit}`}
              value={`${fmt(forecast.predicted, meta.digits)} ${meta.unit}`}
              tone={
                forecast.trend === "up"
                  ? "text-rose-300"
                  : forecast.trend === "down"
                    ? "text-sky-300"
                    : "text-white"
              }
            />
          </div>
        </div>
      </div>

      <div className="mt-5 rounded-2xl border border-slate-800 bg-slate-950/60 px-4 py-3 text-xs text-slate-400">
        Raw run duration: <span className="text-slate-200">{runDuration}</span>. Current view:
        each point is the average inside one{" "}
        <span className="text-slate-200">{mode.label.toLowerCase().slice(0, -1) || mode.label}</span>{" "}
        bucket. Y-axis:{" "}
        <span className="text-slate-200">
          {yScaleMode === "context"
            ? "farm/context range with threshold bands"
            : "zoomed to this run's min/max values"}
        </span>
        .
      </div>

      <div className="mt-5 overflow-x-auto overflow-y-hidden rounded-[1.5rem] border border-slate-800 bg-slate-950/85">
        <svg
          ref={chartRef}
          viewBox={`0 0 ${width} ${height}`}
          style={{ minWidth: `${width}px` }}
          className="h-[430px] cursor-crosshair select-none"
          onMouseMove={handleMouseMove}
          onMouseLeave={() => setHoverPoint(null)}
        >
          <rect x="0" y="0" width={width} height={height} fill="rgba(2,6,23,0.9)" />

          {(meta.thresholds ?? []).map((zone) => {
            const zoneTop = clamp(zone.to, yMin, yMax);
            const zoneBottom = clamp(zone.from, yMin, yMax);

            if (zoneTop <= yMin || zoneBottom >= yMax) return null;

            return (
              <g key={`${zone.label}-${zone.from}-${zone.to}`}>
                <rect
                  x={padL}
                  y={sy(zoneTop)}
                  width={width - padL - padR}
                  height={Math.max(0, sy(zoneBottom) - sy(zoneTop))}
                  fill={zone.color}
                />

                <text
                  x={padL + 10}
                  y={clamp(sy(zoneTop) + 16, padT + 14, height - padB - 8)}
                  fontSize="11"
                  fill="rgba(226,232,240,0.42)"
                >
                  {zone.label}
                </text>
              </g>
            );
          })}

          {gridYs.map((gy, idx) => (
            <line
              key={idx}
              x1={padL}
              y1={gy}
              x2={width - padR}
              y2={gy}
              stroke="rgba(148,163,184,0.12)"
            />
          ))}

          {visibleTicks.map((step) => (
            <g key={`tick-${step}`}>
              <line
                x1={sx(step)}
                y1={padT}
                x2={sx(step)}
                y2={height - padB}
                stroke="rgba(148,163,184,0.08)"
              />

              <text
                x={sx(step)}
                y={height - 24}
                textAnchor="middle"
                fontSize="12"
                fill="rgba(148,163,184,0.72)"
              >
                {formatStepTime(step, timeMode)}
              </text>
            </g>
          ))}

          <line
            x1={padL}
            y1={height - padB}
            x2={width - padR}
            y2={height - padB}
            stroke="rgba(148,163,184,0.2)"
          />

          <line
            x1={padL}
            y1={padT}
            x2={padL}
            y2={height - padB}
            stroke="rgba(148,163,184,0.2)"
          />

          {avgY !== null && (
            <>
              <line
                x1={padL}
                y1={avgY}
                x2={width - padR}
                y2={avgY}
                stroke="rgba(226,232,240,0.35)"
                strokeDasharray="6 6"
              />

              <text
                x={width - padR - 6}
                y={avgY - 7}
                textAnchor="end"
                fontSize="12"
                fill="rgba(226,232,240,0.7)"
              >
                avg {fmt(stats.avg, meta.digits)} {meta.unit}
              </text>
            </>
          )}

          {emptyPoints.map((p) => (
            <circle
              key={`empty-${p.step}`}
              cx={sx(p.x)}
              cy={height - padB}
              r="3"
              fill="rgba(148,163,184,0.2)"
            />
          ))}

          {actualPoints.length > 1 && (
            <polyline
              fill="none"
              stroke={meta.color}
              strokeWidth="3.5"
              strokeLinecap="round"
              strokeLinejoin="round"
              points={actualPolyline}
            />
          )}

          {actualPoints.map((p) => (
            <circle key={`actual-${p.step}`} cx={sx(p.x)} cy={sy(p.y)} r="4" fill={meta.color} />
          ))}

          {lastActual && predictionPoints.length > 0 && (
            <>
              <rect
                x={sx(lastActual.x)}
                y={padT}
                width={Math.max(0, sx(maxStep) - sx(lastActual.x))}
                height={height - padT - padB}
                fill={meta.soft}
              />

              <polyline
                fill="none"
                stroke={meta.color}
                strokeWidth="2.5"
                strokeDasharray="7 6"
                strokeLinecap="round"
                strokeLinejoin="round"
                points={predictionPolyline}
              />

              <text
                x={sx(lastActual.x) + 12}
                y={padT + 22}
                fontSize="12"
                fill={meta.color}
              >
                prediction: next {FORECAST_STEPS} {mode.axisUnit}-steps
              </text>

              <text
                x={sx(lastActual.x) + 12}
                y={padT + 40}
                fontSize="12"
                fill="rgba(226,232,240,0.78)"
              >
                {trendText(forecast.trend)} · Δ {fmt(forecast.delta, meta.digits)}{" "}
                {meta.unit} · fit {(forecast.confidence * 100).toFixed(0)}%
              </text>

              {predictionPoints.map((p, idx) => (
                <g key={`prediction-${p.step}`}>
                  <rect
                    x={sx(p.x) - 4}
                    y={sy(p.y) - 4}
                    width="8"
                    height="8"
                    transform={`rotate(45 ${sx(p.x)} ${sy(p.y)})`}
                    fill={meta.color}
                  />

                  <text
                    x={sx(p.x)}
                    y={clamp(sy(p.y) - 12, padT + 12, height - padB - 10)}
                    textAnchor="middle"
                    fontSize="11"
                    fill={meta.color}
                  >
                    +{idx + 1}
                    {mode.axisUnit}
                  </text>
                </g>
              ))}
            </>
          )}

          {minPoint && (
            <>
              <circle cx={sx(minPoint.x)} cy={sy(minPoint.y)} r="5" fill={meta.color} />

              <text
                x={clamp(sx(minPoint.x) + 8, padL + 8, width - 130)}
                y={clamp(sy(minPoint.y) + 18, padT + 16, height - padB - 10)}
                fontSize="12"
                fill="rgba(226,232,240,0.85)"
              >
                min {fmt(minPoint.y, meta.digits)}
              </text>
            </>
          )}

          {maxPoint && (
            <>
              <circle cx={sx(maxPoint.x)} cy={sy(maxPoint.y)} r="5" fill={meta.color} />

              <text
                x={clamp(sx(maxPoint.x) + 8, padL + 8, width - 130)}
                y={clamp(sy(maxPoint.y) - 12, padT + 12, height - padB - 10)}
                fontSize="12"
                fill="rgba(226,232,240,0.85)"
              >
                max {fmt(maxPoint.y, meta.digits)}
              </text>
            </>
          )}

          {lastActual && (
            <>
              <circle cx={sx(lastActual.x)} cy={sy(lastActual.y)} r="5.5" fill="#ffffff" />

              <text
                x={clamp(sx(lastActual.x) - 8, padL + 80, width - 12)}
                y={clamp(sy(lastActual.y) - 12, padT + 12, height - padB - 10)}
                textAnchor="end"
                fontSize="12"
                fill="#ffffff"
              >
                now {fmt(lastActual.y, meta.digits)}
              </text>
            </>
          )}

          {hoverPoint && (
            <>
              <line
                x1={hoverX}
                y1={padT}
                x2={hoverX}
                y2={height - padB}
                stroke="rgba(226,232,240,0.45)"
                strokeDasharray="4 5"
              />

              <circle
                cx={hoverX}
                cy={hoverY}
                r="6"
                fill="rgba(15,23,42,1)"
                stroke={hoverPoint.isPrediction ? "#ffffff" : meta.color}
                strokeWidth="3"
              />

              <g>
                <rect
                  x={tooltipX}
                  y={tooltipY}
                  width="236"
                  height="102"
                  rx="16"
                  fill="rgba(15,23,42,0.96)"
                  stroke="rgba(148,163,184,0.28)"
                />

                <text
                  x={tooltipX + 14}
                  y={tooltipY + 23}
                  fontSize="12"
                  fill="rgba(148,163,184,1)"
                >
                  {hoverPoint.isPrediction
                    ? `prediction step: +${hoverPoint.step - lastActual.x}${mode.axisUnit}`
                    : `bucket: ${formatStepTime(hoverPoint.step, timeMode)}`}
                </text>

                <text
                  x={tooltipX + 14}
                  y={tooltipY + 44}
                  fontSize="11"
                  fill="rgba(148,163,184,0.78)"
                >
                  {hoverPoint.isPrediction
                    ? `projected after ${compactTime(hoverPoint.tSec)}`
                    : `${hoverPoint.sampleCount} sample${hoverPoint.sampleCount === 1 ? "" : "s"} averaged`}
                </text>

                <text
                  x={tooltipX + 14}
                  y={tooltipY + 73}
                  fontSize="22"
                  fontWeight="700"
                  fill="#ffffff"
                >
                  {fmt(hoverPoint.y, meta.digits)} {meta.unit}
                </text>

                <text
                  x={tooltipX + 14}
                  y={tooltipY + 94}
                  fontSize="12"
                  fill={hoverPoint.isPrediction ? "#ffffff" : meta.color}
                >
                  {hoverPoint.isPrediction ? "Predicted" : meta.label}
                </text>
              </g>
            </>
          )}

          <text x="12" y={padT + 4} fontSize="12" fill="rgba(148,163,184,0.8)">
            {fmt(yMax, meta.digits)}
          </text>

          <text
            x="12"
            y={height - padB + 4}
            fontSize="12"
            fill="rgba(148,163,184,0.8)"
          >
            {fmt(yMin, meta.digits)}
          </text>
        </svg>
      </div>
    </div>
  );
}

function ForecastStrip({ forecasts, timeMode }) {
  const mode = TIME_MODES[timeMode];

  return (
    <div className="rounded-[2rem] border border-slate-800 bg-slate-900/65 p-5">
      <div className="flex items-center justify-between gap-3">
        <div className="text-lg font-semibold text-white">Short-Term Projection</div>

        <div className="rounded-full border border-slate-700 px-3 py-1 text-xs font-semibold uppercase tracking-[0.2em] text-slate-400">
          next {FORECAST_STEPS} {mode.axisUnit}-steps
        </div>
      </div>

      <div className="mt-4 grid gap-3 md:grid-cols-2 xl:grid-cols-4">
        {METRIC_KEYS.map((key) => {
          const meta = METRICS[key];
          const fc = forecasts[key];

          const trendColor =
            fc.trend === "up"
              ? "text-rose-300"
              : fc.trend === "down"
                ? "text-sky-300"
                : "text-slate-200";

          return (
            <div
              key={key}
              className="rounded-3xl border border-slate-800 bg-slate-950/70 p-4"
            >
              <div className="text-[11px] uppercase tracking-[0.22em] text-slate-500">
                {meta.label}
              </div>

              <div className="mt-2 flex items-end justify-between gap-4">
                <div>
                  <div className={`text-3xl font-semibold ${trendColor}`}>
                    {trendArrow(fc.trend)} {fmt(fc.predicted, meta.digits)}
                  </div>

                  <div className="mt-1 text-sm text-slate-400">{meta.unit}</div>
                </div>

                <div className="text-right text-sm text-slate-400">
                  <div>Δ {fmt(fc.delta, meta.digits)}</div>
                  <div>fit {(fc.confidence * 100).toFixed(0)}%</div>
                </div>
              </div>

              <div className="mt-3 text-xs leading-5 text-slate-500">
                {describePrediction(key, fc, timeMode)}
              </div>
            </div>
          );
        })}
      </div>
    </div>
  );
}

function ZoneBar({ zones, stability, risk }) {
  return (
    <div className="rounded-[2rem] border border-slate-800 bg-slate-900/65 p-5">
      <div className="flex flex-wrap items-start justify-between gap-4">
        <div className="min-w-0 flex-1">
          <div className="text-lg font-semibold text-white">Observed Conditions</div>

          <div className="mt-3 h-4 w-full overflow-hidden rounded-full bg-slate-950">
            <div className="flex h-full w-full">
              {zones.map((z) => (
                <div
                  key={z.key}
                  style={{ width: `${z.pct * 100}%`, backgroundColor: z.color }}
                  title={`${z.label}: ${(z.pct * 100).toFixed(1)}%`}
                />
              ))}
            </div>
          </div>

          <div className="mt-4 flex flex-wrap gap-2">
            {zones.map((z) => (
              <div
                key={z.key}
                className="rounded-full border border-slate-800 bg-slate-950/70 px-3 py-1.5 text-sm text-slate-200"
              >
                <span
                  className="mr-2 inline-block h-2.5 w-2.5 rounded-full"
                  style={{ backgroundColor: z.color }}
                />
                {z.label} {(z.pct * 100).toFixed(1)}%
              </div>
            ))}
          </div>
        </div>

        <div className="grid grid-cols-2 gap-3">
          <TinyKpi
            label="Stability"
            value={`${stability.toFixed(0)}/100`}
            tone={
              stability >= 75
                ? "text-emerald-300"
                : stability >= 50
                  ? "text-amber-300"
                  : "text-rose-300"
            }
          />

          <TinyKpi
            label="Risk Share"
            value={`${risk.toFixed(1)}%`}
            tone={
              risk <= 10
                ? "text-emerald-300"
                : risk <= 35
                  ? "text-amber-300"
                  : "text-rose-300"
            }
          />
        </div>
      </div>
    </div>
  );
}

function correlationCellStyle(value) {
  const v = num(value);

  if (v === null) {
    return {
      backgroundColor: "rgba(51,65,85,0.4)",
      color: "#e2e8f0",
    };
  }

  if (v >= 0) {
    const alpha = 0.12 + Math.abs(v) * 0.4;

    return {
      backgroundColor: `rgba(16,185,129,${alpha})`,
      color: "#d1fae5",
    };
  }

  const alpha = 0.12 + Math.abs(v) * 0.4;

  return {
    backgroundColor: `rgba(239,68,68,${alpha})`,
    color: "#fecaca",
  };
}

function CorrelationMatrix({ correlations }) {
  return (
    <div className="rounded-[2rem] border border-slate-800 bg-slate-900/65 p-5">
      <div className="text-lg font-semibold text-white">Correlations</div>

      <div className="mt-4 overflow-auto">
        <div
          className="grid gap-2"
          style={{
            gridTemplateColumns: `120px repeat(${METRIC_KEYS.length}, minmax(92px, 1fr))`,
          }}
        >
          <div />

          {METRIC_KEYS.map((col) => (
            <div
              key={col}
              className="px-2 text-center text-xs font-semibold uppercase tracking-[0.18em] text-slate-500"
            >
              {METRICS[col].short}
            </div>
          ))}

          {METRIC_KEYS.map((row) => (
            <div key={row} className="contents">
              <div className="flex items-center px-2 text-sm font-semibold text-slate-300">
                {METRICS[row].short}
              </div>

              {METRIC_KEYS.map((col) => {
                const value = correlations[row]?.[col];

                return (
                  <div
                    key={`${row}-${col}`}
                    className="rounded-2xl border border-slate-800 px-2 py-4 text-center text-sm font-semibold"
                    style={correlationCellStyle(value)}
                  >
                    {fmt(value, 2)}
                  </div>
                );
              })}
            </div>
          ))}
        </div>
      </div>
    </div>
  );
}

export default function MicroclimatePanel({ payload = null, sessionLabel = null }) {
  const [data, setData] = useState(null);
  const [error, setError] = useState(null);
  const [activeMetric, setActiveMetric] = useState("tempC");
  const [timeMode, setTimeMode] = useState("seconds");
  const [yScaleMode, setYScaleMode] = useState("context");

  useEffect(() => {
    setData(payload ?? null);
    setError(null);
  }, [payload]);

  const analysis = useMemo(() => {
    if (!data) return null;

    const rawSeries = normalizeSeries(data.series ?? []);
    const aggregatedSeries = aggregateSeriesByTimeMode(rawSeries, timeMode);

    const statsMap = Object.fromEntries(
      METRIC_KEYS.map((key) => [key, metricStats(rawSeries, key)]),
    );

    const aggregatedStatsMap = Object.fromEntries(
      METRIC_KEYS.map((key) => [key, metricStats(aggregatedSeries, key)]),
    );

    const forecasts = Object.fromEntries(
      METRIC_KEYS.map((key) => [
        key,
        forecastMetricFromAggregated(aggregatedSeries, key, timeMode),
      ]),
    );

    const envValidity =
      num(data.validity?.envPct) ??
      (num(data.sampleCount) && num(data.totalEntries)
        ? data.sampleCount / data.totalEntries
        : 0);

    const zones = buildZoneDistribution(rawSeries);
    const stability = stabilityScore(statsMap, envValidity);
    const risk = zoneRiskScore(zones);

    const correlations =
      data.correlations && typeof data.correlations.tempHumidity !== "undefined"
        ? {
            tempC: {
              tempC: 1,
              humidityPct: data.correlations.tempHumidity,
              pressureHpa: data.correlations.tempPressure,
              gasKohm: data.correlations.tempGas,
            },
            humidityPct: {
              tempC: data.correlations.tempHumidity,
              humidityPct: 1,
              pressureHpa: data.correlations.humidityPressure,
              gasKohm: data.correlations.humidityGas,
            },
            pressureHpa: {
              tempC: data.correlations.tempPressure,
              humidityPct: data.correlations.humidityPressure,
              pressureHpa: 1,
              gasKohm: data.correlations.pressureGas,
            },
            gasKohm: {
              tempC: data.correlations.tempGas,
              humidityPct: data.correlations.humidityGas,
              pressureHpa: data.correlations.pressureGas,
              gasKohm: 1,
            },
          }
        : computeCorrelations(rawSeries);

    return {
      rawSeries,
      aggregatedSeries,
      statsMap,
      aggregatedStatsMap,
      forecasts,
      zones,
      correlations,
      envValidity,
      totalEntries: num(data.totalEntries) ?? rawSeries.length,
      sampleCount: num(data.sampleCount) ?? rawSeries.length,
      validity: data.validity ?? {},
      stability,
      risk,
    };
  }, [data, timeMode]);

  const activeStats = analysis ? analysis.aggregatedStatsMap[activeMetric] : null;
  const activeForecast = analysis ? analysis.forecasts[activeMetric] : null;
  const mode = TIME_MODES[timeMode];

  return (
    <div className="space-y-6">
      <section className="overflow-hidden rounded-[2rem] border border-slate-800 bg-[radial-gradient(circle_at_top_left,_rgba(59,130,246,0.18),_transparent_24%),radial-gradient(circle_at_top_right,_rgba(16,185,129,0.16),_transparent_24%),linear-gradient(180deg,rgba(2,6,23,0.98),rgba(15,23,42,0.92))] p-6 shadow-[0_28px_90px_rgba(2,6,23,0.55)]">
        <div className="flex flex-wrap items-start justify-between gap-4">
          <div>
            <div className="text-[11px] font-semibold uppercase tracking-[0.34em] text-emerald-300">
              Data Analysis
            </div>

            <h1 className="mt-3 text-4xl font-semibold tracking-tight text-white">
              M5Stick Session Analysis
            </h1>

            <div className="mt-3 flex flex-wrap gap-2">
              <div className="rounded-full border border-slate-700 bg-slate-950/60 px-3 py-1.5 text-sm text-slate-200">
                {analysis ? `${analysis.sampleCount} ENV samples` : "…"}
              </div>
              {sessionLabel ? (
                <div className="rounded-full border border-cyan-400/25 bg-cyan-400/10 px-3 py-1.5 text-sm text-cyan-100">
                  {sessionLabel}
                </div>
              ) : null}

              <div className="rounded-full border border-slate-700 bg-slate-950/60 px-3 py-1.5 text-sm text-slate-200">
                {analysis ? compactTime(analysis.rawSeries.at(-1)?.tSec ?? 0) : "…"} raw run
              </div>

              <div className="rounded-full border border-slate-700 bg-slate-950/60 px-3 py-1.5 text-sm text-slate-200">
                {analysis ? mode.bucketLabel : "…"}
              </div>

              <div className="rounded-full border border-slate-700 bg-slate-950/60 px-3 py-1.5 text-sm text-slate-200">
                {Y_SCALE_MODES[yScaleMode].label} Y-axis
              </div>

              <div className="rounded-full border border-slate-700 bg-slate-950/60 px-3 py-1.5 text-sm text-slate-200">
                +{FORECAST_STEPS}
                {mode.axisUnit} projection
              </div>
            </div>
          </div>

          {analysis && (
            <div className="grid grid-cols-2 gap-3">
              <TinyKpi
                label="ENV Valid"
                value={`${(analysis.envValidity * 100).toFixed(1)}%`}
                tone={qualityTone(analysis.envValidity)}
              />

              <TinyKpi
                label="IMU Valid"
                value={analysis.validity.imuPct == null ? "—" : `${(analysis.validity.imuPct * 100).toFixed(1)}%`}
                tone={analysis.validity.imuPct == null ? "slate" : qualityTone(analysis.validity.imuPct)}
              />
            </div>
          )}
        </div>
      </section>

      {error && (
        <div className="rounded-2xl border border-rose-500/30 bg-rose-500/10 px-4 py-3 text-sm text-rose-200">
          {error}
        </div>
      )}

      {!analysis ? (
        <div className="rounded-3xl border border-slate-800 bg-slate-900/65 p-8 text-sm text-slate-300">
          Loading selected-session M5Stick data...
        </div>
      ) : (
        <>
          <section className="grid gap-4 xl:grid-cols-4">
            {METRIC_KEYS.map((key) => (
              <MetricButton
                key={key}
                metricKey={key}
                series={analysis.rawSeries}
                stats={analysis.statsMap[key]}
                forecast={analysis.forecasts[key]}
                selected={activeMetric === key}
                onClick={setActiveMetric}
              />
            ))}
          </section>

          <MainChart
            rawSeries={analysis.rawSeries}
            aggregatedSeries={analysis.aggregatedSeries}
            metricKey={activeMetric}
            stats={activeStats}
            forecast={activeForecast}
            timeMode={timeMode}
            setTimeMode={setTimeMode}
            yScaleMode={yScaleMode}
            setYScaleMode={setYScaleMode}
          />

          <ForecastStrip forecasts={analysis.forecasts} timeMode={timeMode} />

          <section className="grid gap-4 xl:grid-cols-[1.2fr_1fr]">
            <ZoneBar
              zones={analysis.zones}
              stability={analysis.stability}
              risk={analysis.risk}
            />

            <CorrelationMatrix correlations={analysis.correlations} />
          </section>
        </>
      )}
    </div>
  );
}
