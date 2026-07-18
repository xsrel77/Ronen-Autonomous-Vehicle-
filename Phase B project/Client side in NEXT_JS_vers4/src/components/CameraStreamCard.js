"use client";

import { useState } from "react";

function CameraBadge({ label, tone = "neutral" }) {
  const tones = {
    neutral: "border-slate-700 bg-slate-900/70 text-slate-300",
    info: "border-cyan-500/30 bg-cyan-500/10 text-cyan-200",
    success: "border-emerald-500/30 bg-emerald-500/10 text-emerald-200",
    warning: "border-amber-500/30 bg-amber-500/10 text-amber-200",
  };

  return (
    <span
      className={`inline-flex items-center rounded-full border px-3 py-1 text-[11px] font-semibold uppercase tracking-[0.24em] ${tones[tone]}`}
    >
      {label}
    </span>
  );
}

export default function CameraStreamCard({
  title = "Camera Stream",
  initialOn = false,
  onToggle,
  telemetry,
}) {
  const [isOn, setIsOn] = useState(initialOn);
  const frame = telemetry?.perception?.frame;
  const tracking = telemetry?.perception?.tracking;
  const best = telemetry?.perception?.best_detection;

  function toggle() {
    const next = !isOn;
    setIsOn(next);
    if (typeof onToggle === "function") onToggle(next);
  }

  return (
    <div className="overflow-hidden rounded-[2rem] border border-slate-800 bg-slate-900/65 shadow-[0_18px_60px_rgba(2,6,23,0.3)]">
      <div className="flex items-start justify-between gap-4 border-b border-slate-800 px-5 py-4">
        <div>
          <div className="text-[11px] font-semibold uppercase tracking-[0.28em] text-slate-500">
            Vision Console
          </div>
          <div className="mt-2 text-xl font-semibold text-white">{title}</div>
          <div className="mt-1 text-sm text-slate-400">
            Live debug feed metadata from IMX219 and YOLO pipeline
          </div>
        </div>

        <button
          onClick={toggle}
          className={`rounded-full border px-4 py-2 text-sm font-semibold transition ${
            isOn
              ? "border-rose-500/40 bg-rose-500/10 text-rose-200 hover:bg-rose-500/20"
              : "border-cyan-500/40 bg-cyan-500/10 text-cyan-200 hover:bg-cyan-500/20"
          }`}
        >
          {isOn ? "Hide Feed" : "Show Feed"}
        </button>
      </div>

      <div className="p-5">
        <div className="relative aspect-video overflow-hidden rounded-[1.5rem] border border-slate-800 bg-[radial-gradient(circle_at_20%_20%,rgba(16,185,129,0.15),transparent_24%),radial-gradient(circle_at_80%_0%,rgba(56,189,248,0.18),transparent_22%),linear-gradient(180deg,rgba(2,6,23,0.94),rgba(15,23,42,0.94))]">
          <div className="absolute inset-0 bg-[linear-gradient(rgba(148,163,184,0.08)_1px,transparent_1px),linear-gradient(90deg,rgba(148,163,184,0.08)_1px,transparent_1px)] bg-[size:36px_36px]" />

          <div className="absolute left-4 top-4 flex flex-wrap gap-2">
            <CameraBadge
              label={isOn ? "Metadata live" : "Feed hidden"}
              tone={isOn ? "success" : "neutral"}
            />
            <CameraBadge
              label={tracking?.enabled ? "Tracking enabled" : "Tracking idle"}
              tone={tracking?.enabled ? "info" : "warning"}
            />
            <CameraBadge
              label={
                frame?.width && frame?.height
                  ? `${frame.width}x${frame.height}`
                  : "No frame"
              }
            />
          </div>

          <div className="absolute inset-0 flex flex-col items-center justify-center px-6 text-center">
            <div className="rounded-full border border-white/10 bg-white/5 px-4 py-2 text-[11px] font-semibold uppercase tracking-[0.3em] text-slate-300 backdrop-blur">
              {best ? `Target ${best.label}` : "No target locked"}
            </div>
            <div className="mt-4 text-2xl font-semibold text-white">
              {best
                ? `${best.label} at ${best.confidence_pct}% confidence`
                : "Detector waiting for scene content"}
            </div>
            <div className="mt-2 max-w-sm text-sm leading-6 text-slate-400">
              {isOn
                ? "This panel is ready for future video embedding. Right now it surfaces frame metadata, tracking state, and best detection from your debug JSON."
                : "Enable the panel to inspect frame size, tracker offset, and best detection directly from the robot snapshot."}
            </div>
          </div>

          <div className="absolute bottom-4 left-4 right-4 grid gap-2 sm:grid-cols-3">
            <div className="rounded-2xl border border-slate-800 bg-slate-950/75 px-3 py-2">
              <div className="text-[11px] uppercase tracking-[0.22em] text-slate-500">
                Best Detection
              </div>
              <div className="mt-1 text-sm font-semibold text-white">
                {best ? best.label : "None"}
              </div>
            </div>
            <div className="rounded-2xl border border-slate-800 bg-slate-950/75 px-3 py-2">
              <div className="text-[11px] uppercase tracking-[0.22em] text-slate-500">
                Tracker Offset
              </div>
              <div className="mt-1 text-sm font-semibold text-white">
                {tracking?.target_selected
                  ? `${(tracking.target_offset_x ?? 0).toFixed(1)}, ${(tracking.target_offset_y ?? 0).toFixed(1)}`
                  : "Not selected"}
              </div>
            </div>
            <div className="rounded-2xl border border-slate-800 bg-slate-950/75 px-3 py-2">
              <div className="text-[11px] uppercase tracking-[0.22em] text-slate-500">
                Detector State
              </div>
              <div className="mt-1 text-sm font-semibold text-white">
                {telemetry?.health?.detector_running ? "Running" : "Stopped"}
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}
