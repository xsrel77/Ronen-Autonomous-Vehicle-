function TelemetryChip({ label, value, hint, accent = "bg-slate-500" }) {
  return (
    <div className="min-w-[150px] rounded-2xl border border-slate-800 bg-slate-950/75 px-4 py-3">
      <div className="flex items-center gap-2 text-[11px] font-semibold uppercase tracking-[0.24em] text-slate-500">
        <span className={`h-2.5 w-2.5 rounded-full ${accent}`} />
        {label}
      </div>
      <div className="mt-2 text-lg font-semibold text-white">{value}</div>
      <div className="mt-1 text-xs text-slate-500">{hint}</div>
    </div>
  );
}

export default function TelemetryBar({ t, compact = false, className = "" }) {
  const wrapClass = compact ? "gap-3" : "gap-4";
  const statusText = t.robot?.status_text ?? "No status";
  const detections = t.perception?.detection_count ?? 0;
  const lidarConfidence = t.derived?.lidarConfidencePct ?? 0;
  const temp = t.env?.temperatureC ?? 0;
  const humidity = t.env?.humidityPct ?? 0;

  return (
    <div
      className={`rounded-[2rem] border border-slate-800 bg-slate-900/65 p-4 ${className}`}
    >
      <div className={`flex flex-wrap ${wrapClass}`}>
        <TelemetryChip
          label="Robot Mode"
          value={t.robot?.mode ?? "Unknown"}
          hint={statusText}
          accent="bg-cyan-400"
        />
        <TelemetryChip
          label="Drive"
          value={`${(t.drive?.forward_speed ?? 0).toFixed(2)} / ${(t.drive?.steering_speed ?? 0).toFixed(2)}`}
          hint={`cmd ${t.drive?.last_command?.type ?? "N/A"} from ${t.drive?.last_command?.source ?? "unknown"}`}
          accent="bg-violet-400"
        />
        <TelemetryChip
          label="Navigation"
          value={t.navigation?.pose_source ?? "None"}
          hint={`goal ${(t.navigation?.goal?.distance_m ?? 0).toFixed(2)} m`}
          accent={t.navigation?.valid ? "bg-emerald-400" : "bg-amber-400"}
        />
        <TelemetryChip
          label="LiDAR"
          value={`${lidarConfidence}%`}
          hint={`${t.lidar?.snapshot?.point_count ?? 0} points | nearest ${
            t.derived?.nearestObstacleM != null
              ? `${t.derived.nearestObstacleM.toFixed(2)} m`
              : "n/a"
          }`}
          accent={t.lidar?.pose?.valid ? "bg-emerald-400" : "bg-rose-400"}
        />
        <TelemetryChip
          label="ENV"
          value={`${temp.toFixed(1)}°C`}
          hint={`${humidity.toFixed(1)}% RH | ${(t.env?.gasKohm ?? 0).toFixed(1)} kΩ`}
          accent={t.env?.valid ? "bg-amber-300" : "bg-rose-400"}
        />
        <TelemetryChip
          label="Vision"
          value={`${detections} detections`}
          hint={
            t.perception?.best_detection
              ? `${t.perception.best_detection.label} ${t.perception.best_detection.confidence_pct}%`
              : "no active target"
          }
          accent={t.perception?.detections_valid ? "bg-fuchsia-400" : "bg-slate-500"}
        />
        <TelemetryChip
          label="Replay Loop"
          value={`${t.stream?.loopProgressPct ?? 0}%`}
          hint={`frame ${(t.stream?.index ?? 0) + 1}/${t.stream?.totalEntries ?? 0} every ${t.stream?.stepMs ?? 0} ms`}
          accent="bg-sky-300"
        />
      </div>
    </div>
  );
}
