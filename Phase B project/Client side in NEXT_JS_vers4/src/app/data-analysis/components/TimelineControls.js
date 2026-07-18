"use client";

import { useEffect, useMemo, useRef, useState } from "react";
import { TIME_SCALE_OPTIONS } from "../lib/sessionTimeline";

function scaleLabel(scale) {
  if (scale === "seconds") return "sec";
  if (scale === "minutes") return "min";
  if (scale === "hours") return "hour";
  return "day";
}

function compactClockLabel(bucket) {
  return bucket?.label ?? "—";
}

function maturityPercentages(bucket) {
  const groups = bucket?.maturityGroups ?? {
    green: 0,
    turning: 0,
    ripe: 0,
    total: 0,
  };
  const total = Math.max(Number(groups.total) || 0, 1);

  return {
    green: ((Number(groups.green) || 0) / total) * 100,
    turning: ((Number(groups.turning) || 0) / total) * 100,
    ripe: ((Number(groups.ripe) || 0) / total) * 100,
  };
}

function buildAreaPath(series, height, topPad, bottomPad, y0Key, y1Key) {
  if (!series.length) return "";

  const usableHeight = height - topPad - bottomPad;
  const yFor = (value) => topPad + ((100 - value) / 100) * usableHeight;

  const upper = series
    .map(
      (point, index) =>
        `${index === 0 ? "M" : "L"} ${point.x.toFixed(2)} ${yFor(
          point[y1Key],
        ).toFixed(2)}`,
    )
    .join(" ");

  const lower = [...series]
    .reverse()
    .map((point) => `L ${point.x.toFixed(2)} ${yFor(point[y0Key]).toFixed(2)}`)
    .join(" ");

  return `${upper} ${lower} Z`;
}

function Metric({ label, value, detail }) {
  return (
    <div className="rounded-2xl border border-slate-700/80 bg-slate-950/75 px-3 py-2.5">
      <div className="text-[9px] font-semibold uppercase tracking-[0.2em] text-slate-500">
        {label}
      </div>
      <div className="mt-1 text-base font-semibold leading-none text-white">
        {value}
      </div>
      {detail ? (
        <div className="mt-1 text-[10px] text-slate-500">{detail}</div>
      ) : null}
    </div>
  );
}

function ControlButton({ children, onClick, active = false, title }) {
  return (
    <button
      type="button"
      title={title}
      onClick={onClick}
      className={`rounded-xl border px-3 py-2 text-xs font-semibold transition ${
        active
          ? "border-cyan-300/70 bg-cyan-300 text-slate-950"
          : "border-slate-700 bg-slate-900 text-slate-200 hover:border-cyan-300 hover:text-white"
      }`}
    >
      {children}
    </button>
  );
}

function LegendItem({ color, label }) {
  return (
    <span className="inline-flex items-center gap-1.5 text-[11px] text-slate-300">
      <span
        className="h-2.5 w-2.5 rounded-sm"
        style={{ backgroundColor: color }}
      />
      {label}
    </span>
  );
}

function MaturityTimelineGraph({
  buckets,
  selectedBucketIndex,
  onSelectBucket,
  timeScale,
}) {
  const scrollRef = useRef(null);
  const height = 210;
  const topPad = 16;
  const bottomPad = 44;
  const unitWidthByScale = {
    seconds: 12,
    minutes: 42,
    hours: 72,
    days: 96,
  };
  const unitWidth = unitWidthByScale[timeScale] ?? 42;
  const width = Math.max(760, buckets.length * unitWidth);
  const usableHeight = height - topPad - bottomPad;
  const safeSelectedIndex = Math.min(
    Math.max(selectedBucketIndex ?? 0, 0),
    Math.max(0, buckets.length - 1),
  );
  const selectedX = safeSelectedIndex * unitWidth + unitWidth / 2;
  const tickEvery = Math.max(1, Math.ceil(78 / unitWidth));

  const series = buckets.map((bucket, index) => {
    const percent = maturityPercentages(bucket);
    const ripeTop = percent.ripe;
    const turningTop = ripeTop + percent.turning;
    const greenTop = turningTop + percent.green;

    return {
      x: index * unitWidth + unitWidth / 2,
      ripe0: 0,
      ripe1: ripeTop,
      turning0: ripeTop,
      turning1: turningTop,
      green0: turningTop,
      green1: greenTop,
      maturity: Number(bucket?.avgMaturityPercent) || 0,
    };
  });

  useEffect(() => {
    const container = scrollRef.current;
    if (!container || !buckets.length) return;

    const target = Math.max(0, selectedX - container.clientWidth / 2);
    container.scrollTo({ left: target, behavior: "smooth" });
  }, [buckets.length, selectedX, timeScale]);

  function selectAtClientX(event) {
    const svg = event.currentTarget;
    const rect = svg.getBoundingClientRect();
    const ratio = width / Math.max(rect.width, 1);
    const x = (event.clientX - rect.left) * ratio;
    const index = Math.min(
      Math.max(Math.floor(x / unitWidth), 0),
      buckets.length - 1,
    );
    onSelectBucket(index);
  }

  return (
    <div className="mt-4 rounded-3xl border border-slate-800 bg-slate-950/70 p-4">
      <div className="flex flex-wrap items-center justify-between gap-3">
        <div>
          <div className="text-sm font-semibold text-white">Maturity timeline</div>
          <p className="mt-1 text-xs text-slate-400">
            Stacked percentage view. Each {scaleLabel(timeScale)} has equal width;
            empty periods keep the last known map state.
          </p>
        </div>
        <div className="flex flex-wrap items-center gap-3">
          <LegendItem color="#f43f5e" label="Ripe" />
          <LegendItem color="#f59e0b" label="Turning / mixed" />
          <LegendItem color="#22c55e" label="Green / unripe" />
        </div>
      </div>

      <div
        ref={scrollRef}
        className="mt-4 overflow-x-auto overflow-y-hidden rounded-2xl border border-slate-800 bg-slate-900/60 pb-2 overscroll-x-contain"
      >
        <svg
          width={width}
          height={height}
          className="block cursor-crosshair select-none"
          role="img"
          aria-label="Cumulative real-session tomato maturity timeline"
          onClick={selectAtClientX}
        >
          <rect x="0" y="0" width={width} height={height} fill="rgba(2,6,23,0.55)" />

          {[0, 25, 50, 75, 100].map((value) => {
            const y = topPad + ((100 - value) / 100) * usableHeight;
            return (
              <g key={value}>
                <line
                  x1="0"
                  x2={width}
                  y1={y}
                  y2={y}
                  stroke="rgba(148,163,184,0.16)"
                  strokeDasharray="4 8"
                />
                <text
                  x="8"
                  y={y - 4}
                  fill="rgba(203,213,225,0.68)"
                  fontSize="11"
                >
                  {value}%
                </text>
              </g>
            );
          })}

          <path
            d={buildAreaPath(series, height, topPad, bottomPad, "ripe0", "ripe1")}
            fill="rgba(244,63,94,0.82)"
          />
          <path
            d={buildAreaPath(series, height, topPad, bottomPad, "turning0", "turning1")}
            fill="rgba(245,158,11,0.82)"
          />
          <path
            d={buildAreaPath(series, height, topPad, bottomPad, "green0", "green1")}
            fill="rgba(34,197,94,0.78)"
          />

          <polyline
            points={series
              .map(
                (point) =>
                  `${point.x.toFixed(2)},${(
                    topPad + ((100 - point.maturity) / 100) * usableHeight
                  ).toFixed(2)}`,
              )
              .join(" ")}
            fill="none"
            stroke="rgba(255,255,255,0.82)"
            strokeWidth="2"
            strokeLinecap="round"
            strokeLinejoin="round"
          />

          {buckets.map((bucket, index) => {
            const x = index * unitWidth + unitWidth / 2;
            const major = index % tickEvery === 0 || index === buckets.length - 1;
            return (
              <g key={bucket.id}>
                <line
                  x1={x}
                  x2={x}
                  y1={height - bottomPad + 8}
                  y2={height - bottomPad + (major ? 20 : 14)}
                  stroke="rgba(148,163,184,0.46)"
                />
                {major ? (
                  <text
                    x={x}
                    y={height - 10}
                    textAnchor="middle"
                    fill="rgba(203,213,225,0.76)"
                    fontSize="11"
                  >
                    {bucket.label}
                  </text>
                ) : null}
              </g>
            );
          })}

          <rect
            x={safeSelectedIndex * unitWidth}
            y="0"
            width={unitWidth}
            height={height - bottomPad + 24}
            fill="rgba(56,189,248,0.08)"
          />
          <line
            x1={selectedX}
            x2={selectedX}
            y1="0"
            y2={height - 18}
            stroke="rgba(56,189,248,0.95)"
            strokeWidth="2.5"
          />
          <circle cx={selectedX} cy={topPad + 4} r="4" fill="#38bdf8" />
        </svg>
      </div>
    </div>
  );
}

export default function TimelineControls({
  buckets = [],
  bucketPosition,
  setBucketPosition,
  layer,
  setLayer,
  timeScale,
  setTimeScale,
}) {
  const [playing, setPlaying] = useState(false);
  const safeIndex = Math.max(
    0,
    Math.min(bucketPosition ?? 0, Math.max(0, buckets.length - 1)),
  );
  const selected = buckets[safeIndex] ?? null;
  const progress =
    buckets.length > 1
      ? Math.round((safeIndex / (buckets.length - 1)) * 100)
      : 100;

  useEffect(() => {
    setPlaying(false);
  }, [buckets.length, timeScale]);

  useEffect(() => {
    if (!playing || buckets.length < 2) return undefined;

    const timer = window.setInterval(() => {
      setBucketPosition((current) => {
        const currentIndex = Number.isFinite(current) ? current : 0;
        if (currentIndex >= buckets.length - 1) {
          setPlaying(false);
          return buckets.length - 1;
        }
        return currentIndex + 1;
      });
    }, 430);

    return () => window.clearInterval(timer);
  }, [playing, buckets.length, setBucketPosition]);

  const selectedGroupSummary = useMemo(() => {
    const values = maturityPercentages(selected);
    return `${Math.round(values.green)}% green · ${Math.round(
      values.turning,
    )}% turning · ${Math.round(values.ripe)}% ripe`;
  }, [selected]);

  if (!buckets.length) {
    return (
      <div className="border-t border-slate-800 bg-slate-950/90 px-4 py-4 text-sm text-slate-400">
        No timestamped selected-session observations are available for timeline playback.
      </div>
    );
  }

  return (
    <div className="border-t border-slate-800 bg-slate-950/95 p-4">
      <div className="flex flex-col gap-4 xl:flex-row xl:items-center xl:justify-between">
        <div className="min-w-0">
          <div className="text-[10px] font-semibold uppercase tracking-[0.28em] text-purple-300">
            Scan Playback
          </div>
          <div className="mt-1 flex flex-wrap items-baseline gap-x-2 gap-y-1">
            <h3 className="text-base font-semibold text-white">
              Cumulative session timeline
            </h3>
            <span className="text-xs text-slate-500">
              {compactClockLabel(selected)} · {safeIndex + 1}/{buckets.length}
            </span>
          </div>
        </div>

        <div className="grid grid-cols-3 gap-2 sm:min-w-[360px]">
          <Metric
            label="Findings"
            value={selected?.totalKnownDetections ?? 0}
            detail="known landmarks"
          />
          <Metric
            label="Updates"
            value={selected?.updateCount ?? 0}
            detail="this step"
          />
          <Metric
            label="Progress"
            value={`${progress}%`}
            detail={selectedGroupSummary}
          />
        </div>
      </div>

      <div className="mt-4 flex flex-wrap items-center gap-2">
        <ControlButton
          onClick={() => {
            setPlaying(false);
            setBucketPosition(0);
          }}
        >
          Start
        </ControlButton>
        <ControlButton
          onClick={() => {
            setPlaying(false);
            setBucketPosition((value) => Math.max(0, (value ?? safeIndex) - 1));
          }}
        >
          Prev
        </ControlButton>
        <ControlButton active={playing} onClick={() => setPlaying((value) => !value)}>
          {playing ? "Pause" : "Play"}
        </ControlButton>
        <ControlButton
          onClick={() => {
            setPlaying(false);
            setBucketPosition((value) =>
              Math.min(buckets.length - 1, (value ?? safeIndex) + 1),
            );
          }}
        >
          Next
        </ControlButton>
        <ControlButton
          onClick={() => {
            setPlaying(false);
            setBucketPosition(buckets.length - 1);
          }}
        >
          End
        </ControlButton>

        <div className="mx-1 hidden h-7 w-px bg-slate-700 sm:block" />

        <div className="flex flex-wrap items-center gap-1.5">
          {Object.keys(TIME_SCALE_OPTIONS).map((scale) => (
            <button
              key={scale}
              type="button"
              onClick={() => {
                setPlaying(false);
                setTimeScale(scale);
                setBucketPosition(0);
              }}
              className={`rounded-lg border px-2.5 py-1.5 text-[10px] font-semibold uppercase tracking-[0.12em] transition ${
                timeScale === scale
                  ? "border-purple-300/70 bg-purple-300/15 text-purple-100"
                  : "border-slate-700 bg-slate-900 text-slate-400 hover:border-slate-500 hover:text-slate-200"
              }`}
            >
              {scaleLabel(scale)}
            </button>
          ))}
        </div>

        <div className="ml-auto flex items-center gap-1.5">
          {[
            ["observed", "Observed"],
            ["kriging", "Kriging"],
          ].map(([value, label]) => (
            <button
              key={value}
              type="button"
              onClick={() => setLayer(value)}
              className={`rounded-lg border px-2.5 py-1.5 text-[10px] font-semibold uppercase tracking-[0.12em] transition ${
                layer === value
                  ? "border-emerald-300/70 bg-emerald-300/15 text-emerald-100"
                  : "border-slate-700 bg-slate-900 text-slate-400 hover:border-slate-500 hover:text-slate-200"
              }`}
            >
              {label}
            </button>
          ))}
        </div>
      </div>

      <MaturityTimelineGraph
        buckets={buckets}
        selectedBucketIndex={safeIndex}
        timeScale={timeScale}
        onSelectBucket={(index) => {
          setPlaying(false);
          setBucketPosition(index);
        }}
      />
    </div>
  );
}
