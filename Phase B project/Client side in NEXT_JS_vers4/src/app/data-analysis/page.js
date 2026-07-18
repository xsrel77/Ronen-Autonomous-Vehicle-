"use client";

import { useEffect, useMemo, useState } from "react";
import DataAnalysisSessionPicker from "./components/DataAnalysisSessionPicker";
import GreenhouseMap from "./components/GreenhouseMap";
import MicroclimatePanel from "./components/MicroclimatePanel";
import PcaPanel from "./components/PcaPanel";
import ResearchRagPanel from "./components/ResearchRagPanel";
import { buildSessionTimelineBuckets, getRobotPoseAtOrBefore } from "./lib/sessionTimeline";
import { summarizeSpatialModel } from "./lib/spatialModel";

const TABS = [
  { id: "overview", label: "Overview" },
  { id: "map", label: "Greenhouse Map" },
  { id: "microclimate", label: "Microclimate / M5Stick" },
  { id: "pca", label: "PCA" },
  { id: "quality", label: "Data Quality" },
  { id: "rag", label: "Research RAG" },
];

const SESSION_STORAGE_KEY = "ecofarm-data-analysis-selected-session";
const SESSION_PICKER_HIDDEN_KEY = "ecofarm-data-analysis-session-picker-hidden";

function formatNumber(value, digits = 1) {
  return Number.isFinite(value) ? value.toFixed(digits) : "—";
}

function StatCard({ label, value, detail, tone = "slate" }) {
  const toneClass = {
    emerald: "border-emerald-400/20 bg-emerald-400/10 text-emerald-100",
    cyan: "border-cyan-400/20 bg-cyan-400/10 text-cyan-100",
    fuchsia: "border-fuchsia-400/20 bg-fuchsia-400/10 text-fuchsia-100",
    amber: "border-amber-400/20 bg-amber-400/10 text-amber-100",
    slate: "border-slate-700 bg-slate-900/70 text-slate-100",
  }[tone] ?? "border-slate-700 bg-slate-900/70 text-slate-100";

  return (
    <div className={`rounded-3xl border p-4 ${toneClass}`}>
      <div className="text-[10px] font-semibold uppercase tracking-[0.24em] opacity-70">{label}</div>
      <div className="mt-2 text-2xl font-semibold text-white">{value}</div>
      <div className="mt-1 text-sm text-slate-400">{detail}</div>
    </div>
  );
}

function TabButton({ tab, activeTab, setActiveTab }) {
  const active = tab.id === activeTab;
  return (
    <button
      type="button"
      onClick={() => setActiveTab(tab.id)}
      className={`rounded-full border px-4 py-2 text-xs font-semibold uppercase tracking-[0.18em] transition ${
        active
          ? "border-emerald-400/60 bg-emerald-400/15 text-emerald-100"
          : "border-slate-700 bg-slate-950/70 text-slate-400 hover:border-slate-500 hover:text-white"
      }`}
    >
      {tab.label}
    </button>
  );
}

function OverviewPanel({ payload, activeLandmarks, spatialSummary, setActiveTab }) {
  const latestEnvironment = payload?.environment?.series?.at(-1) ?? null;
  const averageMaturity = activeLandmarks.length
    ? activeLandmarks.reduce((sum, item) => sum + (item.maturityScore ?? 0.5), 0) / activeLandmarks.length
    : null;

  return (
    <section className="space-y-6">
      <div className="grid gap-4 md:grid-cols-2 xl:grid-cols-4">
        <StatCard label="M5Stick records" value={payload?.environment?.sampleCount ?? 0} detail="Real environment rows from robot_timeline.jsonl" tone="emerald" />
        <StatCard label="Known landmarks" value={activeLandmarks.length} detail="Grouped accepted YOLO observations at this timeline state" tone="cyan" />
        <StatCard label="Avg maturity index" value={averageMaturity == null ? "—" : `${Math.round(averageMaturity * 100)}%`} detail="Class-derived maturity index" tone="amber" />
        <StatCard label="Moran’s I" value={formatNumber(spatialSummary?.moran?.value, 2)} detail={spatialSummary?.moran?.label ?? "Not enough anchors"} tone="fuchsia" />
      </div>

      <div className="grid gap-6 xl:grid-cols-[1.1fr_0.9fr]">
        <div className="rounded-[2rem] border border-slate-800 bg-slate-900/65 p-5">
          <div className="text-[11px] font-semibold uppercase tracking-[0.3em] text-emerald-300">Shared selected-session analysis</div>
          <h2 className="mt-2 text-2xl font-semibold text-white">One real data source across the ecological workflow</h2>
          <div className="mt-5 grid gap-3 md:grid-cols-2">
            <button type="button" onClick={() => setActiveTab("microclimate")} className="rounded-3xl border border-slate-800 bg-slate-950/70 p-4 text-left transition hover:border-emerald-400/50">
              <div className="text-sm font-semibold text-white">Microclimate / M5Stick</div>
              <p className="mt-2 text-sm leading-6 text-slate-400">Real temperature, humidity, pressure, and only the sensor channels that are actually exported by this session.</p>
            </button>
            <button type="button" onClick={() => setActiveTab("map")} className="rounded-3xl border border-slate-800 bg-slate-950/70 p-4 text-left transition hover:border-cyan-400/50">
              <div className="text-sm font-semibold text-white">Greenhouse spatial map</div>
              <p className="mt-2 text-sm leading-6 text-slate-400">Real accepted YOLO landmarks, timeline replay, class colors, click-to-inspect frames, and ordinary Kriging maturity estimates.</p>
            </button>
          </div>
        </div>

        <div className="rounded-[2rem] border border-slate-800 bg-slate-900/65 p-5">
          <div className="text-[11px] font-semibold uppercase tracking-[0.3em] text-cyan-300">Latest selected-session sensor state</div>
          <h2 className="mt-2 text-2xl font-semibold text-white">M5Stick snapshot</h2>
          <div className="mt-5 grid grid-cols-2 gap-3">
            <StatCard label="Temp" value={latestEnvironment?.tempC != null ? `${formatNumber(latestEnvironment.tempC)}°C` : "—"} detail="Temperature" />
            <StatCard label="Humidity" value={latestEnvironment?.humidityPct != null ? `${formatNumber(latestEnvironment.humidityPct)}%` : "—"} detail="Relative humidity" />
            <StatCard label="Pressure" value={latestEnvironment?.pressureHpa != null ? `${formatNumber(latestEnvironment.pressureHpa)} hPa` : "—"} detail="Air pressure" />
            <StatCard label="Gas" value={latestEnvironment?.gasKohm != null ? `${formatNumber(latestEnvironment.gasKohm)} kΩ` : "Not exported"} detail="No synthetic replacement is used" />
          </div>
        </div>
      </div>
    </section>
  );
}

function DataQualityPanel({ payload, activeLandmarks }) {
  const quality = payload?.quality ?? {};
  const gasAvailable = payload?.environment?.gasAvailable === true;

  return (
    <section className="rounded-[2rem] border border-slate-800 bg-slate-900/65 p-5">
      <div className="text-[11px] font-semibold uppercase tracking-[0.3em] text-amber-300">Selected-session data quality</div>
      <h2 className="mt-2 text-2xl font-semibold text-white">Real data provenance and analysis limits</h2>

      <div className="mt-5 grid gap-4 md:grid-cols-2 xl:grid-cols-4">
        <StatCard label="M5Stick" value="Real session data" detail={`${payload?.environment?.sampleCount ?? 0} parsed environmental records`} tone="emerald" />
        <StatCard label="YOLO" value="Saved map detections" detail={`${quality.acceptedObservationCount ?? 0} accepted observations · ${activeLandmarks.length} active landmarks`} tone="cyan" />
        <StatCard label="Kriging" value="Ordinary Kriging" detail="Spherical variogram over the active real anchors" tone="fuchsia" />
        <StatCard label="Gas channel" value={gasAvailable ? "Exported" : "Not exported"} detail={gasAvailable ? "Available to M5Stick/PCA" : "Excluded; never converted to zero"} tone="amber" />
      </div>

      <div className="mt-5 grid gap-4 lg:grid-cols-2">
        <div className="rounded-3xl border border-slate-800 bg-slate-950/70 p-4">
          <div className="text-sm font-semibold text-white">What this patch now uses</div>
          <ul className="mt-3 space-y-2 text-sm leading-6 text-slate-400">
            <li>• M5Stick environment rows from <code>robot_timeline.jsonl</code>.</li>
            <li>• Accepted tomato detections and saved image paths from <code>detections_on_map.jsonl</code>.</li>
            <li>• Robot route poses from <code>map_pose_timeline.jsonl</code>.</li>
            <li>• One globally selected session shared across Data Analysis tabs.</li>
          </ul>
        </div>
        <div className="rounded-3xl border border-slate-800 bg-slate-950/70 p-4">
          <div className="text-sm font-semibold text-white">Important limits retained in the UI</div>
          <ul className="mt-3 space-y-2 text-sm leading-6 text-slate-400">
            <li>• Tomato coordinates are saved exporter projections and are explicitly marked approximate.</li>
            <li>• The maturity index comes from the saved tomato class, not a measured chemical ripeness value.</li>
            <li>• Kriging is an exploratory spatial estimate over these approximate anchors, not a verified yield map.</li>
            <li>• Weak/suppressed source detections are not used as ecological map anchors.</li>
          </ul>
        </div>
      </div>
    </section>
  );
}

export default function DataAnalysisPage() {
  const [payload, setPayload] = useState(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState(null);
  const [hydrated, setHydrated] = useState(false);
  const [selectedSessionId, setSelectedSessionId] = useState("");
  const [sessionPickerHidden, setSessionPickerHidden] = useState(false);
  const [timeScale, setTimeScale] = useState("seconds");
  const [bucketPosition, setBucketPosition] = useState(0);
  const [layer, setLayer] = useState("kriging");
  const [selectedLandmark, setSelectedLandmark] = useState(null);
  const [activeTab, setActiveTab] = useState("overview");

  useEffect(() => {
    const savedSessionId = window.localStorage.getItem(SESSION_STORAGE_KEY) ?? "";
    const savedHidden = window.localStorage.getItem(SESSION_PICKER_HIDDEN_KEY) === "true";
    setSelectedSessionId(savedSessionId);
    setSessionPickerHidden(savedHidden);
    setHydrated(true);
  }, []);

  useEffect(() => {
    if (!hydrated) return;
    let alive = true;

    async function loadSelectedSession() {
      setLoading(true);
      try {
        const query = selectedSessionId ? `?session=${encodeURIComponent(selectedSessionId)}` : "";
        const response = await fetch(`/api/data-analysis/session${query}`, { cache: "no-store" });
        const nextPayload = await response.json();
        if (!response.ok) throw new Error(nextPayload?.error ?? `HTTP ${response.status}`);
        if (!alive) return;

        setPayload(nextPayload);
        setError(null);
        const resolvedSessionId = nextPayload.selectedSessionId ?? "";
        if (resolvedSessionId !== selectedSessionId) {
          setSelectedSessionId(resolvedSessionId);
          if (resolvedSessionId) window.localStorage.setItem(SESSION_STORAGE_KEY, resolvedSessionId);
        }
      } catch (loadError) {
        if (!alive) return;
        setPayload(null);
        setError(loadError instanceof Error ? loadError.message : "Failed to load selected session data.");
      } finally {
        if (alive) setLoading(false);
      }
    }

    loadSelectedSession();
    return () => { alive = false; };
  }, [hydrated, selectedSessionId]);

  const timelineBuckets = useMemo(
    () => buildSessionTimelineBuckets(payload?.map ?? {}, timeScale),
    [payload?.map, timeScale],
  );
  const safeBucketPosition = Math.max(0, Math.min(bucketPosition, Math.max(0, timelineBuckets.length - 1)));
  const selectedBucket = timelineBuckets[safeBucketPosition] ?? null;

  useEffect(() => {
    if (!timelineBuckets.length) {
      setBucketPosition(0);
      return;
    }
    setBucketPosition(timelineBuckets.length - 1);
  }, [payload?.selectedSessionId, timeScale, timelineBuckets.length]);

  const activeLandmarks = useMemo(() => {
    if (!selectedBucket) return [];
    return (payload?.map?.landmarks ?? []).filter((item) => (item.firstTimestampMs ?? Infinity) <= selectedBucket.endTimestampMs);
  }, [payload?.map?.landmarks, selectedBucket]);

  const currentDetections = useMemo(() => {
    if (!selectedBucket) return [];
    return (payload?.map?.observations ?? []).filter((item) => {
      const timestampMs = item.timestampMs ?? -Infinity;
      return timestampMs >= selectedBucket.startTimestampMs && timestampMs <= selectedBucket.endTimestampMs;
    });
  }, [payload?.map?.observations, selectedBucket]);

  const currentRobotPose = useMemo(
    () => getRobotPoseAtOrBefore(payload?.map?.robotTrail ?? [], selectedBucket?.endTimestampMs),
    [payload?.map?.robotTrail, selectedBucket?.endTimestampMs],
  );

  const spatialSummary = useMemo(
    () => summarizeSpatialModel(activeLandmarks, payload?.map?.layout),
    [activeLandmarks, payload?.map?.layout],
  );

  const microclimatePayload = useMemo(() => {
    if (!payload?.environment) return null;
    return {
      series: payload.environment.series,
      stats: payload.environment.stats,
      sampleCount: payload.environment.sampleCount,
      totalEntries: payload.environment.totalEntries,
      validity: payload.environment.validity,
    };
  }, [payload?.environment]);

  useEffect(() => {
    if (selectedLandmark && !activeLandmarks.some((item) => item.id === selectedLandmark.id)) {
      setSelectedLandmark(null);
    }
  }, [selectedLandmark, activeLandmarks]);

  function handleSessionChange(nextSessionId) {
    setSelectedSessionId(nextSessionId);
    setSelectedLandmark(null);
    window.localStorage.setItem(SESSION_STORAGE_KEY, nextSessionId);
  }

  function toggleSessionPicker() {
    setSessionPickerHidden((current) => {
      const next = !current;
      window.localStorage.setItem(SESSION_PICKER_HIDDEN_KEY, String(next));
      return next;
    });
  }

  return (
    <main className="min-h-screen bg-slate-950 px-4 py-6 text-slate-100 sm:px-6 lg:px-8">
      <div className="mx-auto max-w-[1600px] space-y-6">
        <section className="overflow-hidden rounded-[2rem] border border-slate-800 bg-gradient-to-br from-slate-900 via-slate-950 to-emerald-950/40 p-6 shadow-2xl shadow-black/40">
          <div className="grid gap-6 xl:grid-cols-[1.5fr_0.9fr] xl:items-end">
            <div>
              <div className="text-[11px] font-semibold uppercase tracking-[0.35em] text-emerald-300">Robot EcoFarm · Data Analysis</div>
              <h1 className="mt-4 max-w-5xl text-4xl font-semibold tracking-tight text-white md:text-5xl">Selected-session ecological analysis for greenhouse tomatoes</h1>
              <p className="mt-4 max-w-4xl text-base leading-7 text-slate-300">
                M5Stick telemetry, saved YOLO detections, robot map poses, PCA, and ordinary Kriging are all sourced from one real session under <code>src/session-data</code>. No mock tomato layer, legacy debug JSON, or scenario override is used by this page.
              </p>
            </div>
            <div className="grid grid-cols-2 gap-3 text-sm">
              <div className="rounded-3xl border border-emerald-400/20 bg-emerald-400/10 p-4"><div className="text-[10px] uppercase tracking-[0.25em] text-emerald-200">Source</div><div className="mt-2 font-semibold text-white">Real session-data</div><div className="mt-1 text-slate-400">M5Stick · YOLO · map poses</div></div>
              <div className="rounded-3xl border border-cyan-400/20 bg-cyan-400/10 p-4"><div className="text-[10px] uppercase tracking-[0.25em] text-cyan-200">Spatial model</div><div className="mt-2 font-semibold text-white">Ordinary Kriging</div><div className="mt-1 text-slate-400">class index · uncertainty</div></div>
            </div>
          </div>
        </section>

        <nav className="flex flex-wrap gap-2 rounded-[2rem] border border-slate-800 bg-slate-900/65 p-3">
          {TABS.map((tab) => <TabButton key={tab.id} tab={tab} activeTab={activeTab} setActiveTab={setActiveTab} />)}
        </nav>

        <DataAnalysisSessionPicker
          sessions={payload?.sessions ?? []}
          selectedSessionId={payload?.selectedSessionId ?? selectedSessionId}
          onChange={handleSessionChange}
          hidden={sessionPickerHidden}
          onToggle={toggleSessionPicker}
          loading={loading}
        />

        {error ? <div className="rounded-2xl border border-rose-500/30 bg-rose-500/10 px-4 py-3 text-sm text-rose-200">{error}</div> : null}
        {loading && !payload ? <div className="rounded-3xl border border-slate-800 bg-slate-900/65 p-6 text-sm text-slate-300">Loading real selected-session data…</div> : null}

        {payload && activeTab === "overview" ? <OverviewPanel payload={payload} activeLandmarks={activeLandmarks} spatialSummary={spatialSummary} setActiveTab={setActiveTab} /> : null}

        {payload && activeTab === "map" ? (
          <GreenhouseMap
            layout={payload.map.layout}
            rosMap={payload.map.rosMap}
            classes={payload.classes}
            samples={activeLandmarks}
            currentDetections={currentDetections}
            currentRobotPose={currentRobotPose}
            robotTrail={payload.map.robotTrail}
            spatialSummary={spatialSummary}
            timelineBuckets={timelineBuckets}
            bucketPosition={safeBucketPosition}
            setBucketPosition={setBucketPosition}
            layer={layer}
            setLayer={setLayer}
            timeScale={timeScale}
            setTimeScale={setTimeScale}
            selectedId={selectedLandmark?.id}
            onSelect={setSelectedLandmark}
          />
        ) : null}

        {payload && activeTab === "microclimate" ? <MicroclimatePanel payload={microclimatePayload} sessionLabel={payload.session?.label ?? payload.selectedSessionId} /> : null}
        {payload && activeTab === "pca" ? <PcaPanel envSeries={payload.environment.series} tomatoSamples={activeLandmarks} /> : null}
        {payload && activeTab === "quality" ? <DataQualityPanel payload={payload} activeLandmarks={activeLandmarks} /> : null}
        {activeTab === "rag" ? <ResearchRagPanel /> : null}
      </div>
    </main>
  );
}
