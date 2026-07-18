"use client";

import { useEffect, useMemo, useState } from "react";
import { fetchDetections } from "@/lib/api";

function severityFor(label) {
  if (label === "person") return "danger";
  if (label.includes("disease") || label.includes("rot")) return "warning";
  if (label.includes("weed")) return "info";
  return "success";
}

function badgeClasses(severity) {
  if (severity === "danger") {
    return "border-rose-500/30 bg-rose-500/10 text-rose-200";
  }

  if (severity === "warning") {
    return "border-amber-500/30 bg-amber-500/10 text-amber-200";
  }

  if (severity === "info") {
    return "border-cyan-500/30 bg-cyan-500/10 text-cyan-200";
  }

  return "border-emerald-500/30 bg-emerald-500/10 text-emerald-200";
}

export default function DetectionsPanel({ className = "" }) {
  const [items, setItems] = useState([]);
  const [err, setErr] = useState(null);

  useEffect(() => {
    let alive = true;

    async function tick() {
      try {
        const data = await fetchDetections();
        if (!alive) return;
        setItems(data);
        setErr(null);
      } catch (e) {
        if (!alive) return;
        setErr(e?.message || "Detections error");
      }
    }

    tick();
    const id = setInterval(tick, 1500);

    return () => {
      alive = false;
      clearInterval(id);
    };
  }, []);

  const summary = useMemo(() => {
    return items.reduce((acc, item) => {
      const key = item.label;
      acc[key] = (acc[key] || 0) + 1;
      return acc;
    }, {});
  }, [items]);

  return (
    <div
      className={`rounded-[2rem] border border-slate-800 bg-slate-900/65 p-5 shadow-[0_18px_60px_rgba(2,6,23,0.3)] ${className}`}
    >
      <div className="flex items-start justify-between gap-4">
        <div>
          <div className="text-[11px] font-semibold uppercase tracking-[0.28em] text-slate-500">
            Detection Feed
          </div>
          <div className="mt-2 text-xl font-semibold text-white">
            Real-time Detector Output
          </div>
        </div>
        <div className="rounded-full border border-slate-700 bg-slate-950/70 px-3 py-1 text-xs font-semibold text-slate-300">
          {items.length} objects
        </div>
      </div>

      {err && (
        <div className="mt-4 rounded-2xl border border-rose-500/30 bg-rose-500/10 p-3 text-sm text-rose-200">
          {err}
        </div>
      )}

      <div className="mt-5 grid gap-3 sm:grid-cols-2">
        {Object.entries(summary).map(([label, count]) => (
          <div
            key={label}
            className="rounded-2xl border border-slate-800 bg-slate-950/70 px-4 py-3"
          >
            <div className="text-[11px] uppercase tracking-[0.22em] text-slate-500">
              {label}
            </div>
            <div className="mt-2 text-2xl font-semibold text-white">{count}</div>
          </div>
        ))}
      </div>

      <div className="mt-5 space-y-3 max-h-[420px] overflow-auto pr-1">
        {items.length === 0 && (
          <div className="rounded-2xl border border-slate-800 bg-slate-950/70 p-4 text-sm text-slate-400">
            No detections in the latest frame.
          </div>
        )}

        {items.map((d) => {
          const severity = severityFor(d.label);

          return (
            <div
              key={d.id}
              className="rounded-2xl border border-slate-800 bg-slate-950/70 p-4"
            >
              <div className="flex items-start justify-between gap-3">
                <div>
                  <div className="text-sm font-semibold text-white">
                    {d.label}
                  </div>
                  <div className="mt-1 text-xs text-slate-500">
                    Confidence {d.confidencePct}% | class {d.classId}
                  </div>
                </div>
                <div
                  className={`rounded-full border px-3 py-1 text-[11px] font-semibold uppercase tracking-[0.22em] ${badgeClasses(severity)}`}
                >
                  {severity}
                </div>
              </div>

              <div className="mt-4 grid gap-2 sm:grid-cols-2">
                <div className="rounded-xl border border-slate-800/80 bg-slate-900/70 px-3 py-2">
                  <div className="text-[11px] uppercase tracking-[0.2em] text-slate-500">
                    Bounding Box
                  </div>
                  <div className="mt-1 text-sm font-semibold text-white">
                    {`${Math.round(d.bbox.x)}, ${Math.round(d.bbox.y)} / ${Math.round(d.bbox.w)}x${Math.round(d.bbox.h)}`}
                  </div>
                </div>

                <div className="rounded-xl border border-slate-800/80 bg-slate-900/70 px-3 py-2">
                  <div className="text-[11px] uppercase tracking-[0.2em] text-slate-500">
                    Tracking
                  </div>
                  <div className="mt-1 text-sm font-semibold text-white">
                    {d.tracking.targetSelected
                      ? `${d.tracking.confidencePct}% selected`
                      : "No active track"}
                  </div>
                </div>
              </div>
            </div>
          );
        })}
      </div>
    </div>
  );
}
