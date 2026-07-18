"use client";

import { useEffect, useMemo, useRef, useState } from "react";
import {
  buildEstimatedSessionMap,
  maturityDisplayColor,
  replayTomatoLandmarks,
} from "../lib/estimatedSessionMap";

const SVG_WIDTH = 1120;
const SVG_HEIGHT = 760;
const MAP_PADDING = 56;

function formatNumber(value, digits = 2) {
  return Number.isFinite(value) ? value.toFixed(digits) : "—";
}

function formatPercent(value, digits = 0) {
  return Number.isFinite(value) ? `${(value * 100).toFixed(digits)}%` : "—";
}

function mediaUrl(sessionId, relativePath) {
  if (!sessionId || !relativePath) return null;

  return `/api/robot-debug/media?session=${encodeURIComponent(
    sessionId,
  )}&path=${encodeURIComponent(relativePath)}`;
}

function gridStepForSpan(spanM) {
  if (spanM <= 2) return 0.25;
  if (spanM <= 5) return 0.5;
  if (spanM <= 12) return 1;
  if (spanM <= 30) return 2;
  return 5;
}

function firstGridValue(minimum, step) {
  return Math.ceil(minimum / step) * step;
}

function ToggleButton({ active, onClick, children, title }) {
  return (
    <button
      type="button"
      title={title}
      onClick={onClick}
      className={`rounded-xl border px-3 py-2 text-xs font-semibold transition ${
        active
          ? "border-emerald-400/60 bg-emerald-400/15 text-emerald-100"
          : "border-slate-700 bg-slate-950/70 text-slate-400 hover:border-slate-500 hover:text-white"
      }`}
    >
      {children}
    </button>
  );
}

function SmallMetric({ label, value, detail }) {
  return (
    <div className="rounded-2xl border border-slate-800 bg-slate-950/70 px-3 py-2.5">
      <div className="text-[10px] font-semibold uppercase tracking-[0.14em] text-slate-500">
        {label}
      </div>
      <div className="mt-1 text-base font-semibold text-white">{value}</div>
      {detail ? <div className="mt-1 text-[11px] text-slate-500">{detail}</div> : null}
    </div>
  );
}

function RobotTriangle({ pose, toSvgPoint, color = "#22d3ee", label }) {
  if (!pose) return null;

  const tip = toSvgPoint(
    pose.xM + Math.cos(pose.yawRad) * 0.22,
    pose.yM + Math.sin(pose.yawRad) * 0.22,
  );
  const left = toSvgPoint(
    pose.xM + Math.cos(pose.yawRad + 2.45) * 0.13,
    pose.yM + Math.sin(pose.yawRad + 2.45) * 0.13,
  );
  const right = toSvgPoint(
    pose.xM + Math.cos(pose.yawRad - 2.45) * 0.13,
    pose.yM + Math.sin(pose.yawRad - 2.45) * 0.13,
  );
  const center = toSvgPoint(pose.xM, pose.yM);

  return (
    <g pointerEvents="none">
      <circle
        cx={center.x}
        cy={center.y}
        r="14"
        fill={color}
        fillOpacity="0.16"
        stroke={color}
        strokeOpacity="0.42"
        strokeWidth="1.5"
      />
      <polygon
        points={`${tip.x},${tip.y} ${left.x},${left.y} ${right.x},${right.y}`}
        fill={color}
        stroke="#f8fafc"
        strokeWidth="1.6"
      />
      {label ? (
        <text
          x={center.x + 16}
          y={center.y - 14}
          fill="#e2e8f0"
          fontSize="12"
          fontWeight="700"
        >
          {label}
        </text>
      ) : null}
    </g>
  );
}

function TomatoInspectorModal({ open, onClose, marker, imageSrc }) {
  if (!open || !marker) return null;

  const observation = marker.representative ?? marker.latestObservation ?? null;

  return (
    <div
      className="fixed inset-0 z-[10000] flex items-center justify-center bg-slate-950/85 p-3 backdrop-blur-sm sm:p-6"
      role="dialog"
      aria-modal="true"
      aria-label="Selected tomato landmark"
      onMouseDown={(event) => {
        if (event.target === event.currentTarget) onClose();
      }}
    >
      <div className="max-h-[94vh] w-full max-w-6xl overflow-y-auto rounded-[1.75rem] border border-slate-600 bg-slate-950 shadow-2xl shadow-black/70">
        <div className="sticky top-0 z-10 flex items-start justify-between gap-4 border-b border-slate-800 bg-slate-950/95 px-5 py-4 backdrop-blur">
          <div>
            <div className="text-[11px] font-semibold uppercase tracking-[0.24em] text-amber-300">
              Persistent tomato landmark
            </div>
            <h4 className="mt-1 text-2xl font-semibold text-white">
              {marker.label}
            </h4>
            <p className="mt-1 text-sm text-slate-400">
              {marker.count} accepted sightings accumulated through the selected timeline state.
            </p>
          </div>

          <button
            type="button"
            onClick={onClose}
            className="flex h-10 w-10 shrink-0 items-center justify-center rounded-full border border-slate-700 bg-slate-900 text-2xl leading-none text-slate-200 transition hover:border-cyan-300 hover:text-white"
            aria-label="Close tomato inspector"
          >
            ×
          </button>
        </div>

        <div className="grid gap-5 p-5 lg:grid-cols-[minmax(0,1.45fr)_minmax(300px,0.85fr)]">
          <div className="flex min-h-[420px] items-center justify-center overflow-hidden rounded-2xl border border-slate-800 bg-black/45 p-2 sm:min-h-[560px]">
            {imageSrc ? (
              <img
                src={imageSrc}
                alt={`Full captured ${marker.label} detection image`}
                className="h-auto max-h-[76vh] w-full object-contain"
              />
            ) : (
              <div className="p-6 text-center text-sm text-slate-400">
                No retained image path is available for this tomato landmark.
              </div>
            )}
          </div>

          <div className="space-y-3">
            <div className="grid grid-cols-2 gap-3 text-sm">
              <SmallMetric
                label="Sightings"
                value={marker.count}
                detail={`${marker.updateCount} map updates`}
              />
              <SmallMetric
                label="Average confidence"
                value={formatPercent(marker.avgConfidence)}
                detail={`${marker.stableFrames} stable-best records`}
              />
              <SmallMetric
                label="Raw maturity"
                value={formatNumber(marker.avgMaturityScore, 3)}
                detail="Exported pipeline score"
              />
              <SmallMetric
                label="Map support"
                value={`${marker.mapSupportCount}/${marker.count}`}
                detail="Final LiDAR map surface"
              />
              <SmallMetric
                label="Median zoom"
                value={`${formatNumber(marker.medianDigitalZoom, 2)}×`}
                detail={`${marker.highQualityCount} high · ${marker.mediumQualityCount} medium`}
              />
            </div>

            <div className="rounded-2xl border border-slate-800 bg-slate-900/60 p-3 text-sm">
              <div className="text-xs font-semibold uppercase tracking-[0.14em] text-slate-500">
                Fixed session-local anchor
              </div>
              <div className="mt-2 grid grid-cols-2 gap-2 text-slate-300">
                <span>x forward: {formatNumber(marker.xM)} m</span>
                <span>y left: {formatNumber(marker.yM)} m</span>
              </div>
              <div className="mt-2 text-xs leading-5 text-slate-500">
                Median LiDAR-map ray distance: {formatNumber(marker.medianRangeM)} m
              </div>
            </div>

            <div className="rounded-2xl border border-slate-800 bg-slate-900/60 p-3 text-sm">
              <div className="text-xs font-semibold uppercase tracking-[0.14em] text-slate-500">
                Cumulative state
              </div>
              <div className="mt-2 space-y-1 text-slate-300">
                <div>First seen: {marker.firstTimestampLocal ?? "—"}</div>
                <div>Last seen: {marker.latestTimestampLocal ?? "—"}</div>
                <div>Source tracks: {marker.sourceTrackIds?.length ?? 0}</div>
                <div>Best track hits: {observation?.trackHits ?? "—"}</div>
              </div>
            </div>

            <div className="rounded-2xl border border-slate-800 bg-slate-900/60 p-3 text-sm">
              <div className="text-xs font-semibold uppercase tracking-[0.14em] text-slate-500">
                Best captured-sighting context
              </div>
              <div className="mt-2 grid grid-cols-3 gap-2 text-center text-slate-300">
                <div>
                  <div className="text-xs text-slate-500">Temp</div>
                  <div>{formatNumber(observation?.environment?.temperatureC, 1)}°C</div>
                </div>
                <div>
                  <div className="text-xs text-slate-500">Humidity</div>
                  <div>{formatNumber(observation?.environment?.humidityPct, 1)}%</div>
                </div>
                <div>
                  <div className="text-xs text-slate-500">Pressure</div>
                  <div>{formatNumber(observation?.environment?.pressureHpa, 1)}</div>
                </div>
              </div>
              <div className="mt-2 text-xs leading-5 text-slate-500">
                Camera bearing: {formatNumber(observation?.globalBearingDeg, 1)}° · robot estimate x {formatNumber(observation?.robotXM)} · y {formatNumber(observation?.robotYM)}
              </div>
              <div className="mt-1 text-xs leading-5 text-slate-500">
                Zoom-aware projection: {formatNumber(observation?.digitalZoom, 2)}× · quality {observation?.projectionQuality ?? "—"} · max reasonable range {formatNumber(observation?.maxReasonableDistanceM)} m
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}

export default function EstimatedSessionMap({
  sessionId,
  timeline = [],
  lidarPreview = null,
  detectionEvents = [],
}) {
  const [motionScale, setMotionScale] = useState(0.01);
  const [turnGain, setTurnGain] = useState(0.05);
  const [headingHintBlend, setHeadingHintBlend] = useState(0.45);
  const [headingHintDirection, setHeadingHintDirection] = useState(-1);
  const [cameraFov, setCameraFov] = useState(70);
  const [mapRayWidth, setMapRayWidth] = useState(0.16);
  const [associationDistance, setAssociationDistance] = useState(0.28);
  const [occupancyCellSize, setOccupancyCellSize] = useState(0.12);
  const [minimumSightings, setMinimumSightings] = useState(2);
  const [timelineIndex, setTimelineIndex] = useState(0);
  const [playing, setPlaying] = useState(false);
  const [showOccupancy, setShowOccupancy] = useState(true);
  const [showPointCloud, setShowPointCloud] = useState(false);
  const [showTrail, setShowTrail] = useState(true);
  const [showSelectedRay, setShowSelectedRay] = useState(false);
  const [selectedMarkerId, setSelectedMarkerId] = useState(null);
  const [inspectorOpen, setInspectorOpen] = useState(false);
  const [zoom, setZoom] = useState(1);
  const [pan, setPan] = useState({ x: 0, y: 0 });
  const dragRef = useRef(null);

  const settings = useMemo(
    () => ({
      motionMetersPerCommandSecond: motionScale,
      turnDegreesPerCommandSecond: turnGain,
      headingHintBlend,
      headingHintDirection,
      cameraHorizontalFovDeg: cameraFov,
      mapRayHalfWidthM: mapRayWidth,
      landmarkAssociationDistanceM: associationDistance,
      trackAssociationDistanceM: Math.max(associationDistance + 0.12, 0.3),
      occupancyCellSizeM: occupancyCellSize,
    }),
    [
      motionScale,
      turnGain,
      headingHintBlend,
      headingHintDirection,
      cameraFov,
      mapRayWidth,
      associationDistance,
      occupancyCellSize,
    ],
  );

  const map = useMemo(
    () =>
      buildEstimatedSessionMap(
        { timeline, lidarPreview, detectionEvents },
        settings,
      ),
    [timeline, lidarPreview, detectionEvents, settings],
  );

  useEffect(() => {
    setTimelineIndex(Math.max(map.poses.length - 1, 0));
    setPlaying(false);
    setSelectedMarkerId(null);
    setInspectorOpen(false);
    setZoom(1);
    setPan({ x: 0, y: 0 });
  }, [sessionId, map.poses.length]);

  useEffect(() => {
    if (!playing || map.poses.length < 2) return undefined;

    const timer = window.setInterval(() => {
      setTimelineIndex((current) => {
        if (current >= map.poses.length - 1) {
          setPlaying(false);
          return map.poses.length - 1;
        }

        return current + 1;
      });
    }, 240);

    return () => window.clearInterval(timer);
  }, [playing, map.poses.length]);

  useEffect(() => {
    if (!inspectorOpen) return undefined;

    const handleEscape = (event) => {
      if (event.key === "Escape") setInspectorOpen(false);
    };

    document.addEventListener("keydown", handleEscape);
    return () => document.removeEventListener("keydown", handleEscape);
  }, [inspectorOpen]);

  const safeTimelineIndex = Math.max(
    0,
    Math.min(timelineIndex, Math.max(map.poses.length - 1, 0)),
  );
  const selectedPose = map.poses[safeTimelineIndex] ?? null;
  const selectedTimestampMs = selectedPose?.timestampMs ?? Number.POSITIVE_INFINITY;

  const replay = useMemo(
    () => replayTomatoLandmarks(map.measurements, selectedTimestampMs, settings),
    [map.measurements, selectedTimestampMs, settings],
  );

  const visibleLandmarks = replay.landmarks.filter(
    (landmark) => landmark.count >= minimumSightings,
  );

  const selectedMarker =
    visibleLandmarks.find((landmark) => landmark.id === selectedMarkerId) ??
    visibleLandmarks[0] ??
    null;

  const selectedObservation =
    selectedMarker?.representative ?? selectedMarker?.latestObservation ?? null;

  const imageSrc = mediaUrl(
    sessionId,
    selectedObservation?.annotatedImagePath ?? selectedObservation?.imagePath,
  );

  const drawAreaWidth = SVG_WIDTH - MAP_PADDING * 2;
  const drawAreaHeight = SVG_HEIGHT - MAP_PADDING * 2;
  const metersPerSvgUnit = Math.max(
    map.bounds.widthM / drawAreaWidth,
    map.bounds.heightM / drawAreaHeight,
  );
  const mapWidthSvg = map.bounds.widthM / metersPerSvgUnit;
  const mapHeightSvg = map.bounds.heightM / metersPerSvgUnit;
  const mapLeft = (SVG_WIDTH - mapWidthSvg) / 2;
  const mapTop = (SVG_HEIGHT - mapHeightSvg) / 2;

  function toSvgPoint(xM, yM) {
    return {
      x: mapLeft + (map.bounds.maxYM - yM) / metersPerSvgUnit,
      y: mapTop + (map.bounds.maxXM - xM) / metersPerSvgUnit,
    };
  }

  function mapDistanceToSvg(distanceM) {
    return distanceM / metersPerSvgUnit;
  }

  const fullTrail = map.poses
    .map((pose) => {
      const point = toSvgPoint(pose.xM, pose.yM);
      return `${point.x},${point.y}`;
    })
    .join(" ");

  const progressTrail = map.poses
    .slice(0, safeTimelineIndex + 1)
    .map((pose) => {
      const point = toSvgPoint(pose.xM, pose.yM);
      return `${point.x},${point.y}`;
    })
    .join(" ");

  const forwardGridValues = [];
  const lateralGridValues = [];
  const forwardGridStep = gridStepForSpan(map.bounds.widthM);
  const lateralGridStep = gridStepForSpan(map.bounds.heightM);

  for (
    let value = firstGridValue(map.bounds.minXM, forwardGridStep);
    value <= map.bounds.maxXM + forwardGridStep * 0.01;
    value += forwardGridStep
  ) {
    forwardGridValues.push(Number(value.toFixed(4)));
  }

  for (
    let value = firstGridValue(map.bounds.minYM, lateralGridStep);
    value <= map.bounds.maxYM + lateralGridStep * 0.01;
    value += lateralGridStep
  ) {
    lateralGridValues.push(Number(value.toFixed(4)));
  }

  const startPose = map.poses[0] ?? null;
  const finalPose = map.poses.at(-1) ?? null;
  const sceneTransform = `translate(${SVG_WIDTH / 2} ${SVG_HEIGHT / 2}) scale(${zoom}) translate(${
    -SVG_WIDTH / 2 + pan.x
  } ${-SVG_HEIGHT / 2 + pan.y})`;

  function resetView() {
    setZoom(1);
    setPan({ x: 0, y: 0 });
  }

  function adjustZoom(amount) {
    setZoom((current) => Math.max(0.65, Math.min(4, current + amount)));
  }

  function handlePointerDown(event) {
    if (event.button !== 0) return;

    const svg = event.currentTarget;
    svg.setPointerCapture?.(event.pointerId);
    dragRef.current = {
      clientX: event.clientX,
      clientY: event.clientY,
      startPan: pan,
    };
  }

  function handlePointerMove(event) {
    if (!dragRef.current) return;

    const rect = event.currentTarget.getBoundingClientRect();
    const deltaX =
      ((event.clientX - dragRef.current.clientX) * SVG_WIDTH) /
      Math.max(rect.width, 1) /
      zoom;
    const deltaY =
      ((event.clientY - dragRef.current.clientY) * SVG_HEIGHT) /
      Math.max(rect.height, 1) /
      zoom;

    setPan({
      x: dragRef.current.startPan.x + deltaX,
      y: dragRef.current.startPan.y + deltaY,
    });
  }

  function handlePointerUp(event) {
    dragRef.current = null;
    event.currentTarget.releasePointerCapture?.(event.pointerId);
  }

  function handleWheel(event) {
    event.preventDefault();
    adjustZoom(event.deltaY < 0 ? 0.15 : -0.15);
  }

  function selectLandmark(landmark) {
    setSelectedMarkerId(landmark.id);
    setInspectorOpen(true);
  }

  if (!map.poses.length) {
    return (
      <section className="rounded-[2rem] border border-slate-800 bg-slate-900/65 p-5">
        <div className="text-[11px] font-semibold uppercase tracking-[0.3em] text-emerald-300">
          Session-local map
        </div>
        <h3 className="mt-2 text-2xl font-semibold text-white">
          Estimated Session Map
        </h3>
        <p className="mt-3 text-sm leading-6 text-slate-400">
          The selected session does not contain a usable timestamped robot timeline.
        </p>
      </section>
    );
  }

  return (
    <section className="rounded-[2rem] border border-slate-800 bg-slate-900/65 p-5">
      <div className="flex flex-col gap-5 2xl:flex-row 2xl:items-end 2xl:justify-between">
        <div>
          <div className="text-[11px] font-semibold uppercase tracking-[0.3em] text-emerald-300">
            Real robot session · persistent timeline map
          </div>
          <h3 className="mt-2 text-2xl font-semibold text-white">
            Estimated Session Map
          </h3>
          <p className="mt-2 max-w-4xl text-sm leading-6 text-slate-400">
            The LiDAR occupancy background is built once from all available session scans. The timeline then replays the robot pose and cumulative tomato knowledge in one fixed session-local coordinate frame. Tomato projections are zoom-aware: the camera bearing uses digital zoom to narrow the effective field of view, and zoomed detections are rejected when a far LiDAR-map hit is inconsistent with the visible tomato scale.
          </p>
        </div>

        <div className="grid grid-cols-2 gap-2 sm:grid-cols-4">
          <SmallMetric label="Timeline samples" value={map.poses.length} />
          <SmallMetric label="Final LiDAR cells" value={map.occupancyCells.length} />
          <SmallMetric
            label="Known landmarks"
            value={visibleLandmarks.length}
            detail={`${minimumSightings}+ sightings`}
          />
          <SmallMetric
            label="Unanchored scans"
            value={replay.stats.unanchoredMeasurements}
            detail="not placed as tomatoes"
          />
        </div>
      </div>

      <div className="mt-5 rounded-3xl border border-slate-800 bg-slate-950/70 p-4">
        <div className="flex flex-col gap-4 xl:flex-row xl:items-center xl:justify-between">
          <div className="min-w-0">
            <div className="text-xs font-semibold uppercase tracking-[0.16em] text-cyan-300">
              Timeline replay
            </div>
            <div className="mt-1 text-lg font-semibold text-white">
              {selectedPose?.timestampLocal ?? "—"}
            </div>
            <div className="mt-1 text-sm text-slate-400">
              Sample {safeTimelineIndex + 1} of {map.poses.length} · {replay.stats.acceptedMeasurements} accepted observations processed · {replay.stats.updatedLandmarks} landmark updates
            </div>
          </div>

          <div className="flex flex-wrap items-center gap-2">
            <button
              type="button"
              onClick={() => {
                setPlaying(false);
                setTimelineIndex(0);
              }}
              className="rounded-xl border border-slate-700 bg-slate-900 px-3 py-2 text-xs font-semibold text-slate-300 hover:border-cyan-300 hover:text-white"
            >
              Start
            </button>
            <button
              type="button"
              onClick={() => {
                setPlaying(false);
                setTimelineIndex((current) => Math.max(0, current - 1));
              }}
              className="rounded-xl border border-slate-700 bg-slate-900 px-3 py-2 text-xs font-semibold text-slate-300 hover:border-cyan-300 hover:text-white"
            >
              Prev
            </button>
            <button
              type="button"
              onClick={() => setPlaying((current) => !current)}
              className="rounded-xl border border-emerald-400/40 bg-emerald-400/10 px-4 py-2 text-xs font-semibold text-emerald-100 hover:bg-emerald-400/20"
            >
              {playing ? "Pause" : "Play"}
            </button>
            <button
              type="button"
              onClick={() => {
                setPlaying(false);
                setTimelineIndex((current) => Math.min(map.poses.length - 1, current + 1));
              }}
              className="rounded-xl border border-slate-700 bg-slate-900 px-3 py-2 text-xs font-semibold text-slate-300 hover:border-cyan-300 hover:text-white"
            >
              Next
            </button>
            <button
              type="button"
              onClick={() => {
                setPlaying(false);
                setTimelineIndex(map.poses.length - 1);
              }}
              className="rounded-xl border border-slate-700 bg-slate-900 px-3 py-2 text-xs font-semibold text-slate-300 hover:border-cyan-300 hover:text-white"
            >
              End
            </button>
          </div>
        </div>

        <input
          className="mt-4 w-full"
          type="range"
          min="0"
          max={Math.max(map.poses.length - 1, 0)}
          step="1"
          value={safeTimelineIndex}
          onChange={(event) => {
            setPlaying(false);
            setTimelineIndex(Number(event.target.value));
          }}
          aria-label="Robot session timeline"
        />
      </div>

      <div className="mt-4 grid gap-3 rounded-3xl border border-slate-800 bg-slate-950/55 p-4 xl:grid-cols-4">
        <label className="flex flex-col gap-2">
          <span className="flex items-center justify-between text-xs font-semibold uppercase tracking-[0.14em] text-slate-400">
            Motion scale
            <strong className="normal-case tracking-normal text-cyan-200">
              {formatNumber(motionScale, 3)} m / cmd·s
            </strong>
          </span>
          <input
            type="range"
            min="0.001"
            max="0.05"
            step="0.001"
            value={motionScale}
            onChange={(event) => setMotionScale(Number(event.target.value))}
          />
          <span className="text-xs text-slate-500">Drive-command display calibration.</span>
        </label>

        <label className="flex flex-col gap-2">
          <span className="flex items-center justify-between text-xs font-semibold uppercase tracking-[0.14em] text-slate-400">
            Turn gain
            <strong className="normal-case tracking-normal text-cyan-200">
              {formatNumber(turnGain, 3)}° / cmd·s
            </strong>
          </span>
          <input
            type="range"
            min="0.001"
            max="0.2"
            step="0.001"
            value={turnGain}
            onChange={(event) => setTurnGain(Number(event.target.value))}
          />
          <span className="text-xs text-slate-500">Steering-command display calibration.</span>
        </label>

        <label className="flex flex-col gap-2">
          <span className="flex items-center justify-between text-xs font-semibold uppercase tracking-[0.14em] text-slate-400">
            LiDAR map-ray width
            <strong className="normal-case tracking-normal text-cyan-200">
              {formatNumber(mapRayWidth, 2)} m
            </strong>
          </span>
          <input
            type="range"
            min="0.06"
            max="0.35"
            step="0.01"
            value={mapRayWidth}
            onChange={(event) => setMapRayWidth(Number(event.target.value))}
          />
          <span className="text-xs text-slate-500">Search corridor for static-map support.</span>
        </label>

        <label className="flex flex-col gap-2">
          <span className="flex items-center justify-between text-xs font-semibold uppercase tracking-[0.14em] text-slate-400">
            Same-tomato threshold
            <strong className="normal-case tracking-normal text-cyan-200">
              {formatNumber(associationDistance, 2)} m
            </strong>
          </span>
          <input
            type="range"
            min="0.08"
            max="0.45"
            step="0.01"
            value={associationDistance}
            onChange={(event) => setAssociationDistance(Number(event.target.value))}
          />
          <span className="text-xs text-slate-500">Landmarks merge only when projections are genuinely close.</span>
        </label>
      </div>

      <div className="mt-3 flex flex-wrap items-center gap-2">
        <ToggleButton active={showOccupancy} onClick={() => setShowOccupancy((value) => !value)}>
          Occupancy cells
        </ToggleButton>
        <ToggleButton active={showPointCloud} onClick={() => setShowPointCloud((value) => !value)}>
          Raw LiDAR points
        </ToggleButton>
        <ToggleButton active={showTrail} onClick={() => setShowTrail((value) => !value)}>
          Robot trail
        </ToggleButton>
        <ToggleButton active={showSelectedRay} onClick={() => setShowSelectedRay((value) => !value)}>
          Selected evidence ray
        </ToggleButton>
        <ToggleButton
          active={headingHintDirection < 0}
          onClick={() => setHeadingHintDirection((value) => value * -1)}
          title="Flip only the experimental LiDAR heading-hint interpretation"
        >
          Flip heading hint
        </ToggleButton>
        <button
          type="button"
          onClick={() => setHeadingHintBlend((current) => (current >= 0.45 ? 0.2 : 0.45))}
          className="rounded-xl border border-slate-700 bg-slate-950/70 px-3 py-2 text-xs font-semibold text-slate-400 hover:border-slate-500 hover:text-white"
        >
          Heading blend {Math.round(headingHintBlend * 100)}%
        </button>
        <button
          type="button"
          onClick={() => setCameraFov((current) => (current === 70 ? 90 : 70))}
          className="rounded-xl border border-slate-700 bg-slate-950/70 px-3 py-2 text-xs font-semibold text-slate-400 hover:border-slate-500 hover:text-white"
        >
          Camera FOV {cameraFov}°
        </button>
        <button
          type="button"
          onClick={() => setOccupancyCellSize((current) => (current <= 0.12 ? 0.18 : 0.12))}
          className="rounded-xl border border-slate-700 bg-slate-950/70 px-3 py-2 text-xs font-semibold text-slate-400 hover:border-slate-500 hover:text-white"
        >
          Grid {formatNumber(occupancyCellSize, 2)} m
        </button>
        <div className="ml-1 flex items-center gap-1 rounded-xl border border-slate-800 bg-slate-950/55 p-1">
          {[1, 2, 3].map((count) => (
            <ToggleButton
              key={count}
              active={minimumSightings === count}
              onClick={() => setMinimumSightings(count)}
            >
              {count}+
            </ToggleButton>
          ))}
        </div>
      </div>

      <div className="mt-5 grid gap-5 2xl:grid-cols-[minmax(0,1fr)_340px]">
        <div className="overflow-hidden rounded-3xl border border-slate-800 bg-[#050b14]">
          <div className="flex flex-wrap items-center justify-between gap-3 border-b border-slate-800 bg-slate-950/80 px-4 py-3">
            <div className="text-xs text-slate-400">
              Final LiDAR geometry is static · timeline reveals cumulative tomato knowledge · drag to pan · scroll to zoom · click a tomato marker for the captured image.
            </div>
            <div className="flex items-center gap-2">
              <button
                type="button"
                onClick={() => adjustZoom(-0.2)}
                className="flex h-8 w-8 items-center justify-center rounded-lg border border-slate-700 bg-slate-900 text-lg text-slate-200 hover:border-cyan-300"
                aria-label="Zoom out"
              >
                −
              </button>
              <span className="min-w-[54px] text-center text-xs font-semibold text-cyan-200">
                {Math.round(zoom * 100)}%
              </span>
              <button
                type="button"
                onClick={() => adjustZoom(0.2)}
                className="flex h-8 w-8 items-center justify-center rounded-lg border border-slate-700 bg-slate-900 text-lg text-slate-200 hover:border-cyan-300"
                aria-label="Zoom in"
              >
                +
              </button>
              <button
                type="button"
                onClick={resetView}
                className="rounded-lg border border-slate-700 bg-slate-900 px-3 py-2 text-xs font-semibold text-slate-300 hover:border-cyan-300 hover:text-white"
              >
                Reset
              </button>
            </div>
          </div>

          <div className="aspect-[28/19] min-h-[440px] w-full">
            <svg
              viewBox={`0 0 ${SVG_WIDTH} ${SVG_HEIGHT}`}
              className="h-full w-full touch-none select-none"
              role="img"
              aria-label="Estimated top-down session-local map with a static LiDAR background, timeline robot pose, and persistent tomato landmarks"
              onPointerDown={handlePointerDown}
              onPointerMove={handlePointerMove}
              onPointerUp={handlePointerUp}
              onPointerCancel={handlePointerUp}
              onWheel={handleWheel}
            >
              <defs>
                <clipPath id="persistent-session-map-clip">
                  <rect x={mapLeft} y={mapTop} width={mapWidthSvg} height={mapHeightSvg} rx="14" />
                </clipPath>
                <filter id="persistent-tomato-glow" x="-80%" y="-80%" width="260%" height="260%">
                  <feGaussianBlur stdDeviation="3" result="blur" />
                  <feMerge>
                    <feMergeNode in="blur" />
                    <feMergeNode in="SourceGraphic" />
                  </feMerge>
                </filter>
              </defs>

              <rect width={SVG_WIDTH} height={SVG_HEIGHT} fill="#040a12" />
              <rect
                x={mapLeft}
                y={mapTop}
                width={mapWidthSvg}
                height={mapHeightSvg}
                rx="14"
                fill="#07111e"
                stroke="#334155"
                strokeWidth="1.5"
              />

              <g transform={sceneTransform} clipPath="url(#persistent-session-map-clip)">
                {forwardGridValues.map((xM) => {
                  const start = toSvgPoint(xM, map.bounds.minYM);
                  const end = toSvgPoint(xM, map.bounds.maxYM);
                  return (
                    <line
                      key={`forward-grid-${xM}`}
                      x1={start.x}
                      y1={start.y}
                      x2={end.x}
                      y2={end.y}
                      stroke="#1e293b"
                      strokeWidth="1"
                      strokeDasharray="4 6"
                    />
                  );
                })}

                {lateralGridValues.map((yM) => {
                  const start = toSvgPoint(map.bounds.minXM, yM);
                  const end = toSvgPoint(map.bounds.maxXM, yM);
                  return (
                    <line
                      key={`lateral-grid-${yM}`}
                      x1={start.x}
                      y1={start.y}
                      x2={end.x}
                      y2={end.y}
                      stroke="#1e293b"
                      strokeWidth="1"
                      strokeDasharray="4 6"
                    />
                  );
                })}

                {showOccupancy
                  ? map.occupancyCells.map((cell) => {
                      const topLeft = toSvgPoint(
                        cell.xM + cell.sizeM / 2,
                        cell.yM + cell.sizeM / 2,
                      );
                      const cellSizeSvg = mapDistanceToSvg(cell.sizeM);
                      return (
                        <rect
                          key={cell.id}
                          x={topLeft.x - cellSizeSvg}
                          y={topLeft.y}
                          width={cellSizeSvg}
                          height={cellSizeSvg}
                          fill="#94a3b8"
                          fillOpacity={0.15 + cell.density * 0.7}
                        />
                      );
                    })
                  : null}

                {showPointCloud
                  ? map.lidarPoints.map((point) => {
                      const svgPoint = toSvgPoint(point.estimatedXM, point.estimatedYM);
                      return (
                        <circle
                          key={point.id}
                          cx={svgPoint.x}
                          cy={svgPoint.y}
                          r="1.15"
                          fill="#38bdf8"
                          fillOpacity="0.55"
                        />
                      );
                    })
                  : null}

                {showTrail && fullTrail ? (
                  <polyline
                    points={fullTrail}
                    fill="none"
                    stroke="#14532d"
                    strokeWidth="2"
                    strokeLinejoin="round"
                    strokeLinecap="round"
                    strokeOpacity="0.8"
                  />
                ) : null}

                {showTrail && progressTrail ? (
                  <polyline
                    points={progressTrail}
                    fill="none"
                    stroke="#22c55e"
                    strokeWidth="3.2"
                    strokeLinejoin="round"
                    strokeLinecap="round"
                    strokeOpacity="0.96"
                  />
                ) : null}

                {showSelectedRay && selectedMarker && selectedObservation ? (() => {
                  const start = toSvgPoint(selectedObservation.robotXM, selectedObservation.robotYM);
                  const end = toSvgPoint(selectedMarker.xM, selectedMarker.yM);
                  return (
                    <line
                      x1={start.x}
                      y1={start.y}
                      x2={end.x}
                      y2={end.y}
                      stroke="#facc15"
                      strokeWidth="2.4"
                      strokeDasharray="6 6"
                      strokeOpacity="0.95"
                    />
                  );
                })() : null}

                {visibleLandmarks.map((landmark) => {
                  const point = toSvgPoint(landmark.xM, landmark.yM);
                  const selected = landmark.id === selectedMarker?.id;
                  const color = maturityDisplayColor(landmark.avgMaturityScore, landmark.label);
                  const radius = selected ? 9.5 : 7.5;

                  return (
                    <g
                      key={landmark.id}
                      className="cursor-pointer"
                      role="button"
                      tabIndex={0}
                      aria-label={`Open ${landmark.label} tomato landmark with ${landmark.count} sightings`}
                      onPointerDown={(event) => event.stopPropagation()}
                      onClick={() => selectLandmark(landmark)}
                      onKeyDown={(event) => {
                        if (event.key === "Enter" || event.key === " ") {
                          event.preventDefault();
                          selectLandmark(landmark);
                        }
                      }}
                    >
                      <circle
                        cx={point.x}
                        cy={point.y}
                        r={radius + 4}
                        fill={color}
                        fillOpacity={selected ? "0.22" : "0.12"}
                        filter={selected ? "url(#persistent-tomato-glow)" : undefined}
                      />
                      <circle
                        cx={point.x}
                        cy={point.y}
                        r={radius}
                        fill={color}
                        stroke="#f8fafc"
                        strokeWidth={selected ? "2.4" : "1.4"}
                      />
                      <text
                        x={point.x}
                        y={point.y + 4}
                        textAnchor="middle"
                        fill="#ffffff"
                        fontSize="10"
                        fontWeight="800"
                        pointerEvents="none"
                      >
                        {landmark.count}
                      </text>
                    </g>
                  );
                })}

                <RobotTriangle pose={startPose} toSvgPoint={toSvgPoint} color="#8b5cf6" label="start" />
                <RobotTriangle pose={selectedPose} toSvgPoint={toSvgPoint} color="#22d3ee" label="robot" />
                {finalPose && finalPose.timestampMs !== selectedPose?.timestampMs ? (
                  <RobotTriangle pose={finalPose} toSvgPoint={toSvgPoint} color="#64748b" label="end" />
                ) : null}
              </g>

              <text x={mapLeft + 14} y={mapTop + 22} fill="#cbd5e1" fontSize="12" fontWeight="700">
                SESSION-LOCAL TOP DOWN
              </text>
              <text x={mapLeft + 14} y={mapTop + 40} fill="#64748b" fontSize="11">
                forward ↑ · robot-left ← · final LiDAR geometry · timeline knowledge replay
              </text>
            </svg>
          </div>
        </div>

        <aside className="rounded-3xl border border-slate-800 bg-slate-950/70 p-4">
          <div className="text-[11px] font-semibold uppercase tracking-[0.22em] text-amber-300">
            Timeline state
          </div>
          <div className="mt-2 text-xl font-semibold text-white">
            {selectedPose?.timestampLocal ?? "—"}
          </div>
          <div className="mt-1 text-sm text-slate-400">
            Robot estimate x {formatNumber(selectedPose?.xM)} · y {formatNumber(selectedPose?.yM)} · yaw {formatNumber(selectedPose?.yawDeg, 1)}°
          </div>

          <div className="mt-5 grid grid-cols-2 gap-3">
            <SmallMetric label="New landmarks" value={replay.stats.createdLandmarks} detail="so far" />
            <SmallMetric label="Updates" value={replay.stats.updatedLandmarks} detail="so far" />
            <SmallMetric label="Anchored scans" value={replay.stats.mapSupportedMeasurements} detail="LiDAR-map supported" />
            <SmallMetric label="Current map" value={visibleLandmarks.length} detail={`${minimumSightings}+ only`} />
          </div>

          <div className="mt-5 rounded-2xl border border-slate-800 bg-slate-900/60 p-3 text-sm">
            <div className="text-xs font-semibold uppercase tracking-[0.14em] text-slate-500">
              Selected landmark
            </div>
            {selectedMarker ? (
              <>
                <div className="mt-2 flex items-center gap-2">
                  <span
                    className="h-3 w-3 rounded-full"
                    style={{ backgroundColor: maturityDisplayColor(selectedMarker.avgMaturityScore, selectedMarker.label) }}
                  />
                  <span className="font-semibold text-white">{selectedMarker.label}</span>
                </div>
                <div className="mt-2 text-slate-300">
                  {selectedMarker.count} sightings · confidence {formatPercent(selectedMarker.avgConfidence)}
                </div>
                <div className="mt-1 text-slate-400">
                  zoom {formatNumber(selectedMarker.medianDigitalZoom, 2)}× · high/medium quality {selectedMarker.highQualityCount}/{selectedMarker.mediumQualityCount}
                </div>
                <div className="mt-1 text-slate-400">
                  x {formatNumber(selectedMarker.xM)} · y {formatNumber(selectedMarker.yM)}
                </div>
                <button
                  type="button"
                  onClick={() => setInspectorOpen(true)}
                  className="mt-3 w-full rounded-xl border border-amber-400/35 bg-amber-400/10 px-3 py-2 text-xs font-semibold text-amber-100 hover:bg-amber-400/20"
                >
                  Open captured image and statistics
                </button>
              </>
            ) : (
              <div className="mt-2 text-slate-400">
                No map-supported landmark meets the current repeat-sighting threshold at this timeline time.
              </div>
            )}
          </div>
        </aside>
      </div>

      <p className="mt-4 rounded-2xl border border-amber-400/20 bg-amber-400/5 p-3 text-xs leading-5 text-amber-100/80">
        This prototype intentionally does not place every camera detection at a fixed distance from the robot. A tomato marker is shown only after an accepted detection ray finds support in the final accumulated LiDAR map, and zoomed detections are checked against a zoom-aware range sanity bound before they can anchor or move a landmark. The result is an estimated, persistent debug map—not SLAM and not verified greenhouse coordinates.
      </p>

      <TomatoInspectorModal
        open={inspectorOpen}
        onClose={() => setInspectorOpen(false)}
        marker={selectedMarker}
        imageSrc={imageSrc}
      />
    </section>
  );
}
