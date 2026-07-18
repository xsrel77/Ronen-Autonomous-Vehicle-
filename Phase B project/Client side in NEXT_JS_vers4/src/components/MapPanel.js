"use client";

import { useEffect, useMemo, useRef, useState } from "react";

const PLANT_MIN_HEIGHT_M = 0.2;
const PLANT_MAX_HEIGHT_M = 1.5;
const ROW_DISTANCE_M = 0.85;
const IMAGE_X_SPREAD_M = 0.55;
const SERVO_CENTER_DEAD_ZONE_DEG = 2.5;

function deg2rad(deg) {
  return (deg * Math.PI) / 180;
}

function decodeBase64Bytes(base64) {
  if (!base64 || typeof window === "undefined") return null;

  const binary = window.atob(base64);
  const bytes = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i += 1) bytes[i] = binary.charCodeAt(i);
  return bytes;
}

function formatNumber(value, digits = 2) {
  const num = Number(value);
  return Number.isFinite(num) ? num.toFixed(digits) : "—";
}

function clamp(value, min, max) {
  return Math.min(max, Math.max(min, value));
}

function safeNumber(value, fallback = null) {
  if (value === null || value === undefined || value === "") return fallback;
  const num = Number(value);
  return Number.isFinite(num) ? num : fallback;
}

function readMapHeaderCollapsed() {
  if (typeof window === "undefined") return true;
  try {
    return window.localStorage.getItem("rbv2-dashboard-map-header-collapsed") !== "false";
  } catch {
    return true;
  }
}

function writeMapHeaderCollapsed(value) {
  if (typeof window === "undefined") return;
  try {
    window.localStorage.setItem("rbv2-dashboard-map-header-collapsed", value ? "true" : "false");
  } catch {
    // localStorage can be unavailable; the map should still work.
  }
}

function readMapViewMode() {
  if (typeof window === "undefined") return "map2d";
  try {
    const value = window.localStorage.getItem("rbv2-dashboard-map-view-mode");
    return value === "row3d" ? "row3d" : "map2d";
  } catch {
    return "map2d";
  }
}

function writeMapViewMode(value) {
  if (typeof window === "undefined") return;
  try {
    window.localStorage.setItem("rbv2-dashboard-map-view-mode", value === "row3d" ? "row3d" : "map2d");
  } catch {
    // Ignore storage failures.
  }
}

function readRow3DRotationDeg() {
  if (typeof window === "undefined") return 0;
  try {
    const value = Number(window.localStorage.getItem("rbv2-dashboard-row3d-rotation-deg"));
    return Number.isFinite(value) ? value : 0;
  } catch {
    return 0;
  }
}

function writeRow3DRotationDeg(value) {
  if (typeof window === "undefined") return;
  try {
    window.localStorage.setItem("rbv2-dashboard-row3d-rotation-deg", String(value));
  } catch {
    // Ignore storage failures.
  }
}

function readMapPointsMode() {
  if (typeof window === "undefined") return "frames";
  try {
    const value = window.localStorage.getItem("rbv2-dashboard-map-points-mode");
    return value === "objects" ? "objects" : "frames";
  } catch {
    return "frames";
  }
}

function writeMapPointsMode(value) {
  if (typeof window === "undefined") return;
  try {
    window.localStorage.setItem("rbv2-dashboard-map-points-mode", value === "objects" ? "objects" : "frames");
  } catch {
    // Ignore storage failures.
  }
}

function policyStatusForDetection(detection) {
  const explicit = String(detection?.policyStatus || detection?.policy_status || "").toLowerCase();
  if (["strong", "review", "color_corrected", "heuristic", "weak", "noise"].includes(explicit)) return explicit;
  if (detection?.heuristic || detection?.sourceType === "heuristic" || detection?.source_type === "heuristic") return "heuristic";
  if (detection?.colorCorrectedByPolicy || detection?.color_corrected_by_policy) return "color_corrected";
  if (detection?.reviewCandidate || detection?.review_candidate) return "review";
  if (detection?.weak && String(detection?.rejectReason || detection?.reject_reason || "").toLowerCase().includes("noise")) return "noise";
  return detection?.weak ? "weak" : "strong";
}

function isWeakLikeStatus(status) {
  return status === "weak" || status === "noise";
}

function categoryColor(category) {
  switch (category) {
    case "ripe_tomato":
      return "rgba(239, 68, 68, 0.96)";
    case "unripe_tomato":
      return "rgba(34, 197, 94, 0.96)";
    case "ripe_bunch":
      return "rgba(126, 34, 206, 0.96)";
    case "unripe_bunch":
      return "rgba(249, 115, 22, 0.96)";
    case "mixed_bunch":
      return "rgba(250, 204, 21, 0.96)";
    default:
      return "rgba(148, 163, 184, 0.96)";
  }
}

function detectionColor(itemOrCategory) {
  if (itemOrCategory && typeof itemOrCategory === "object") {
    const status = policyStatusForDetection(itemOrCategory);
    if (status === "heuristic") return "rgba(250, 204, 21, 0.98)";
    if (status === "noise") return "rgba(148, 163, 184, 0.78)";
    if (status === "weak") return categoryColor(itemOrCategory.category);
    if (status === "review") return "rgba(245, 158, 11, 0.96)";
    if (status === "color_corrected") return "rgba(244, 114, 182, 0.96)";
    return categoryColor(itemOrCategory.category);
  }
  return categoryColor(itemOrCategory);
}

function dominantPolicyStatus(detections) {
  const statuses = new Set((detections ?? []).map((detection) => policyStatusForDetection(detection)));
  if (statuses.has("heuristic")) return "heuristic";
  if (statuses.has("color_corrected")) return "color_corrected";
  if (statuses.has("review")) return "review";
  if (statuses.has("strong")) return "strong";
  if (statuses.has("weak")) return "weak";
  if (statuses.has("noise")) return "noise";
  return "strong";
}

function createPreparedDetections(detections, filters = {}) {
  return (detections ?? []).filter((detection) => {
    const category = detection.category ?? "unknown";
    if (filters.categories && filters.categories[category] === false) return false;
    const status = policyStatusForDetection(detection);
    if (filters.quality && filters.quality[status] === false) return false;
    return true;
  });
}

function makeMapImageCanvas(map, mapWidthPx, mapHeightPx) {
  const bytes = decodeBase64Bytes(map?.map?.image?.data);
  if (!bytes?.length || bytes.length !== mapWidthPx * mapHeightPx) return null;

  const offscreen = document.createElement("canvas");
  offscreen.width = mapWidthPx;
  offscreen.height = mapHeightPx;

  const offscreenContext = offscreen.getContext("2d");
  const imageData = offscreenContext.createImageData(mapWidthPx, mapHeightPx);
  for (let i = 0; i < bytes.length; i += 1) {
    const value = bytes[i];
    const out = i * 4;
    imageData.data[out] = value;
    imageData.data[out + 1] = value;
    imageData.data[out + 2] = value;
    imageData.data[out + 3] = 255;
  }
  offscreenContext.putImageData(imageData, 0, 0);
  return offscreen;
}

function robotForwardVector(yawDeg) {
  const yawRad = deg2rad(yawDeg ?? 0);
  return {
    x: Math.sin(yawRad),
    y: -Math.cos(yawRad),
  };
}

function sideNormalFromYaw(yawDeg, side) {
  const forward = robotForwardVector(yawDeg);
  if (side === "right") {
    return { x: -forward.y, y: forward.x };
  }
  return { x: forward.y, y: -forward.x };
}

function inferServoSide(detection) {
  const panRelativeDeg = safeNumber(
    detection?.cameraView?.panRelativeDeg,
    safeNumber(detection?.projection?.cameraPanLeftDeg, 0),
  );

  if (panRelativeDeg <= -SERVO_CENTER_DEAD_ZONE_DEG) return "right";
  if (panRelativeDeg >= SERVO_CENTER_DEAD_ZONE_DEG) return "left";

  const cameraPanLeftDeg = safeNumber(detection?.projection?.cameraPanLeftDeg, 0);
  return cameraPanLeftDeg < 0 ? "right" : "left";
}

function detectionImageCenter(detection) {
  const directX = safeNumber(detection?.imageCenter?.xNorm, null);
  const directY = safeNumber(detection?.imageCenter?.yNorm, null);
  if (directX != null && directY != null) {
    return { x: clamp(directX, 0, 1), y: clamp(directY, 0, 1) };
  }

  const bbox = detection?.bbox;
  const frameWidth = safeNumber(detection?.frame?.width, 1280) || 1280;
  const frameHeight = safeNumber(detection?.frame?.height, 720) || 720;
  if (bbox?.valid && bbox.w > 0 && bbox.h > 0) {
    return {
      x: clamp((bbox.x + bbox.w / 2) / frameWidth, 0, 1),
      y: clamp((bbox.y + bbox.h / 2) / frameHeight, 0, 1),
    };
  }

  return { x: 0.5, y: 0.5 };
}

function buildRow3DPoint(detection) {
  const pose = detection?.robotPose ?? {};
  const robotX = safeNumber(pose.x, safeNumber(detection?.projection?.x, null));
  const robotY = safeNumber(pose.y, safeNumber(detection?.projection?.y, null));
  if (robotX == null || robotY == null) return null;

  const yawDeg = safeNumber(pose.yawDeg, safeNumber(detection?.projection?.mapBearingDeg, 0)) || 0;
  const side = inferServoSide(detection);
  const normal = sideNormalFromYaw(yawDeg, side);
  const forward = robotForwardVector(yawDeg);
  const center = detectionImageCenter(detection);
  const alongOffsetM = (center.x - 0.5) * IMAGE_X_SPREAD_M;
  const z = PLANT_MAX_HEIGHT_M - center.y * (PLANT_MAX_HEIGHT_M - PLANT_MIN_HEIGHT_M);

  return {
    x: robotX + normal.x * ROW_DISTANCE_M + forward.x * alongOffsetM,
    y: robotY + normal.y * ROW_DISTANCE_M + forward.y * alongOffsetM,
    z: clamp(z, PLANT_MIN_HEIGHT_M, PLANT_MAX_HEIGHT_M),
    side,
    center,
    panRelativeDeg: safeNumber(detection?.cameraView?.panRelativeDeg, safeNumber(detection?.projection?.cameraPanLeftDeg, null)),
  };
}


function detectionFrameKey(detection) {
  return detection?.evidenceKey || detection?.image?.rawPath || detection?.image?.path || `${detection?.timestampMs ?? "unknown"}`;
}

function detectionFrameSortValue(detection) {
  const time = safeNumber(detection?.timestampMs, null);
  if (time != null) return time;
  return 0;
}

function dominantCategory(detections) {
  const counts = new Map();
  for (const detection of detections) {
    const category = detection?.category || "unknown";
    counts.set(category, (counts.get(category) || 0) + 1);
  }
  let best = "unknown";
  let bestCount = -1;
  for (const [category, count] of counts.entries()) {
    if (count > bestCount) {
      best = category;
      bestCount = count;
    }
  }
  return best;
}

function average(values, fallback = null) {
  const nums = values.map((value) => Number(value)).filter((value) => Number.isFinite(value));
  if (!nums.length) return fallback;
  return nums.reduce((sum, value) => sum + value, 0) / nums.length;
}

function buildFrameGroups(detections) {
  const groupsMap = new Map();
  for (const detection of detections ?? []) {
    const key = detectionFrameKey(detection);
    const group = groupsMap.get(key) ?? {
      id: key,
      kind: "frame",
      detections: [],
      timestampMs: detectionFrameSortValue(detection),
      timestampLocal: detection?.timestampLocal ?? null,
      evidenceKey: detection?.evidenceKey ?? key,
      imagePath: detection?.image?.path ?? detection?.image?.rawPath ?? "",
    };
    group.detections.push(detection);
    group.timestampMs = Math.min(group.timestampMs, detectionFrameSortValue(detection));
    group.timestampLocal = group.timestampLocal ?? detection?.timestampLocal ?? null;
    groupsMap.set(key, group);
  }

  return [...groupsMap.values()]
    .map((group) => {
      const detectionsInFrame = group.detections.sort((a, b) => {
        const indexA = safeNumber(a?.eventDetectionIndex, 0);
        const indexB = safeNumber(b?.eventDetectionIndex, 0);
        if (indexA !== indexB) return indexA - indexB;
        return String(a?.id ?? "").localeCompare(String(b?.id ?? ""));
      });
      const weakCount = detectionsInFrame.filter((detection) => isWeakLikeStatus(policyStatusForDetection(detection))).length;
      const strongCount = detectionsInFrame.length - weakCount;
      const policyStatus = dominantPolicyStatus(detectionsInFrame);
      const avgConfidencePct = average(detectionsInFrame.map((detection) => detection.confidencePct), null);
      const category = dominantCategory(detectionsInFrame);
      const first = detectionsInFrame[0] ?? null;
      const robotPose = first?.robotPose ?? {};
      const yawDeg = safeNumber(robotPose?.yawDeg, safeNumber(first?.projection?.mapBearingDeg, 0)) || 0;
      const side = first ? inferServoSide(first) : "left";
      const normal = sideNormalFromYaw(yawDeg, side);
      const forward = robotForwardVector(yawDeg);
      const robotX = safeNumber(robotPose?.x, safeNumber(first?.projection?.x, null));
      const robotY = safeNumber(robotPose?.y, safeNumber(first?.projection?.y, null));
      const rowX = robotX == null ? null : robotX + normal.x * ROW_DISTANCE_M;
      const rowY = robotY == null ? null : robotY + normal.y * ROW_DISTANCE_M;
      const projectionX = average(detectionsInFrame.map((detection) => detection?.projection?.x), first?.projection?.x ?? null);
      const projectionY = average(detectionsInFrame.map((detection) => detection?.projection?.y), first?.projection?.y ?? null);

      return {
        ...group,
        detections: detectionsInFrame,
        count: detectionsInFrame.length,
        weakCount,
        strongCount,
        policyStatus,
        avgConfidencePct,
        category,
        categoryLabel: `${detectionsInFrame.length} objects`,
        weak: isWeakLikeStatus(policyStatus),
        side,
        row3d: rowX == null || rowY == null ? null : {
          x: rowX,
          y: rowY,
          z: PLANT_MIN_HEIGHT_M + 0.08,
          side,
          forward,
          yawDeg,
        },
        projection: projectionX == null || projectionY == null ? null : { x: projectionX, y: projectionY, valid: true },
      };
    })
    .sort((a, b) => a.timestampMs - b.timestampMs || a.id.localeCompare(b.id));
}

function sampledTrail(trail, maxPoints = 80) {
  const rows = Array.isArray(trail) ? trail : [];
  if (rows.length <= maxPoints) return rows;
  const step = Math.ceil(rows.length / maxPoints);
  return rows.filter((_, index) => index % step === 0 || index === rows.length - 1);
}

export default function MapPanel({
  map,
  playbackTimeMs = null,
  filters,
  selectedDetectionId,
  onSelectDetection,
  detectionsOverride = null,
  reviewFocusDetection = null,
  height = 650,
}) {
  const canvasRef = useRef(null);
  const wrapperRef = useRef(null);
  const dragRef = useRef({ active: false, x: 0, y: 0, moved: false });
  const [canvasSize, setCanvasSize] = useState({ width: 900, height });
  const [screenDetections, setScreenDetections] = useState([]);
  const [zoom, setZoom] = useState(1);
  const [pan, setPan] = useState({ x: 0, y: 0 });
  const [viewMode, setViewMode] = useState(() => readMapViewMode());
  const [mapPointsMode, setMapPointsMode] = useState(() => readMapPointsMode());
  const [expandedFrameKey, setExpandedFrameKey] = useState(null);
  const [row3DRotationDeg, setRow3DRotationDeg] = useState(() => readRow3DRotationDeg());
  const [mapHeaderCollapsed, setMapHeaderCollapsed] = useState(() => readMapHeaderCollapsed());

  const isRosMap = map?.kind === "rbv2_ros2_slam_dashboard";
  const resolution = Number(map?.map?.resolutionM) || 0.05;
  const mapWidthPx = Number(map?.map?.width) || 1;
  const mapHeightPx = Number(map?.map?.height) || 1;
  const mapWidthM = mapWidthPx * resolution;
  const mapHeightM = mapHeightPx * resolution;

  const currentTimeMs = useMemo(() => {
    if (!isRosMap) return null;
    if (Number.isFinite(playbackTimeMs)) return playbackTimeMs;
    return map?.timeline?.endTimestampMs ?? null;
  }, [isRosMap, map, playbackTimeMs]);

  const trailUntilNow = useMemo(() => {
    if (!isRosMap) return [];
    if (!Number.isFinite(currentTimeMs)) return map?.trail ?? [];
    return (map?.trail ?? []).filter((point) => point.timestampMs <= currentTimeMs);
  }, [isRosMap, map, currentTimeMs]);

  const finalPose = trailUntilNow.at(-1) ?? null;

  const visibleDetections = useMemo(() => {
    if (!isRosMap) return [];
    const filtered = Array.isArray(detectionsOverride)
      ? detectionsOverride
      : createPreparedDetections(map?.detections, filters);
    if (!Number.isFinite(currentTimeMs)) return filtered;
    return filtered.filter((detection) => detection.timestampMs <= currentTimeMs);
  }, [isRosMap, map, filters, currentTimeMs, detectionsOverride]);

  const focusDetection = useMemo(() => {
    if (!reviewFocusDetection?.projection) return null;
    if (Number.isFinite(currentTimeMs) && reviewFocusDetection.timestampMs > currentTimeMs) return null;
    return reviewFocusDetection;
  }, [reviewFocusDetection, currentTimeMs]);

  const row3DDetections = useMemo(() => {
    return visibleDetections
      .map((detection) => {
        const point3d = buildRow3DPoint(detection);
        return point3d ? { ...detection, row3d: point3d } : null;
      })
      .filter(Boolean);
  }, [visibleDetections]);

  const frameGroups = useMemo(() => buildFrameGroups(visibleDetections), [visibleDetections]);
  const row3DFrameGroups = useMemo(() => buildFrameGroups(row3DDetections).filter((frame) => frame.row3d), [row3DDetections]);
  const expandedRow3DDetections = useMemo(() => {
    if (!expandedFrameKey) return [];
    return row3DDetections.filter((detection) => detectionFrameKey(detection) === expandedFrameKey);
  }, [row3DDetections, expandedFrameKey]);

  const rowSideCounts = useMemo(() => {
    return row3DDetections.reduce(
      (counts, detection) => {
        counts[detection.row3d.side] += 1;
        return counts;
      },
      { left: 0, right: 0 },
    );
  }, [row3DDetections]);

  useEffect(() => {
    setZoom(1);
    setPan({ x: 0, y: 0 });
  }, [map?.session?.id, viewMode]);

  useEffect(() => {
    setExpandedFrameKey(null);
  }, [map?.session?.id, mapPointsMode, currentTimeMs]);

  useEffect(() => {
    if (!expandedFrameKey) return;
    if (!frameGroups.some((frame) => frame.id === expandedFrameKey)) setExpandedFrameKey(null);
  }, [expandedFrameKey, frameGroups]);

  useEffect(() => {
    if (!wrapperRef.current) return;

    const observer = new ResizeObserver(([entry]) => {
      const width = Math.max(320, Math.floor(entry.contentRect.width));
      setCanvasSize({ width, height });
    });

    observer.observe(wrapperRef.current);
    return () => observer.disconnect();
  }, [height]);

  function getViewMetrics(nextZoom = zoom, nextPan = pan) {
    const cssWidth = canvasSize.width;
    const cssHeight = canvasSize.height;
    const padding = 26;
    const fitScale = Math.min(
      (cssWidth - padding * 2) / mapWidthPx,
      (cssHeight - padding * 2) / mapHeightPx,
    );
    const scale = fitScale * nextZoom;
    const drawWidth = mapWidthPx * scale;
    const drawHeight = mapHeightPx * scale;
    const offsetX = (cssWidth - drawWidth) / 2 + nextPan.x;
    const offsetY = (cssHeight - drawHeight) / 2 + nextPan.y;

    return { cssWidth, cssHeight, fitScale, scale, drawWidth, drawHeight, offsetX, offsetY };
  }

  function getRow3DMetrics(nextZoom = zoom, nextPan = pan, nextRotationDeg = row3DRotationDeg) {
    const cssWidth = canvasSize.width;
    const cssHeight = canvasSize.height;
    const projectedWidthAtScale1 = mapWidthM + mapHeightM * 0.45;
    const projectedHeightAtScale1 = mapHeightM * 0.62 + mapWidthM * 0.28 + PLANT_MAX_HEIGHT_M * 0.95;
    const fitScale = Math.min(
      (cssWidth - 80) / Math.max(1, projectedWidthAtScale1),
      (cssHeight - 110) / Math.max(1, projectedHeightAtScale1),
    );
    const scale = fitScale * nextZoom;
    const centerX = mapWidthM / 2;
    const centerY = mapHeightM / 2;
    const originX = cssWidth / 2 + nextPan.x;
    const originY = cssHeight * 0.72 + nextPan.y;
    const verticalScale = scale * 0.92;
    const rotationRad = deg2rad(nextRotationDeg);
    const cosR = Math.cos(rotationRad);
    const sinR = Math.sin(rotationRad);

    const rotateAroundCenter = (x, y) => {
      const dx = x - centerX;
      const dy = y - centerY;
      return {
        x: centerX + dx * cosR - dy * sinR,
        y: centerY + dx * sinR + dy * cosR,
      };
    };

    const project = (x, y, z = 0) => {
      const rotated = rotateAroundCenter(x, y);
      return {
        sx: originX + (rotated.x - centerX) * scale + (rotated.y - centerY) * (-scale * 0.35),
        sy: originY + (rotated.x - centerX) * (scale * 0.22) + (rotated.y - centerY) * (scale * 0.55) - z * verticalScale,
      };
    };

    return {
      cssWidth,
      cssHeight,
      scale,
      fitScale,
      centerX,
      centerY,
      originX,
      originY,
      verticalScale,
      rotationDeg: nextRotationDeg,
      project,
    };
  }

  function draw2DMap(ctx, cssWidth, cssHeight) {
    const { scale, drawWidth, drawHeight, offsetX, offsetY } = getViewMetrics();

    const toScreenMeters = (x, y) => ({
      sx: offsetX + (x / resolution) * scale,
      sy: offsetY + (y / resolution) * scale,
    });

    ctx.save();
    ctx.beginPath();
    ctx.rect(offsetX, offsetY, drawWidth, drawHeight);
    ctx.clip();

    const mapCanvas = makeMapImageCanvas(map, mapWidthPx, mapHeightPx);
    if (mapCanvas) {
      ctx.imageSmoothingEnabled = false;
      ctx.drawImage(mapCanvas, offsetX, offsetY, drawWidth, drawHeight);
      ctx.imageSmoothingEnabled = true;
    } else {
      ctx.fillStyle = "rgba(203, 213, 225, 0.16)";
      ctx.fillRect(offsetX, offsetY, drawWidth, drawHeight);
    }

    ctx.strokeStyle = "rgba(15, 23, 42, 0.28)";
    ctx.lineWidth = Math.max(0.8, Math.min(1.5, zoom));
    const gridStepMeters = 0.5;
    for (let x = 0; x <= mapWidthM; x += gridStepMeters) {
      const screen = toScreenMeters(x, 0);
      ctx.beginPath();
      ctx.moveTo(screen.sx, offsetY);
      ctx.lineTo(screen.sx, offsetY + drawHeight);
      ctx.stroke();
    }
    for (let y = 0; y <= mapHeightM; y += gridStepMeters) {
      const screen = toScreenMeters(0, y);
      ctx.beginPath();
      ctx.moveTo(offsetX, screen.sy);
      ctx.lineTo(offsetX + drawWidth, screen.sy);
      ctx.stroke();
    }

    const manualStart = map.summary?.manualStart;
    if (manualStart?.set) {
      const start = toScreenMeters(manualStart.x, manualStart.y);
      ctx.strokeStyle = "rgba(14, 165, 233, 0.95)";
      ctx.lineWidth = 2.5;
      ctx.beginPath();
      ctx.arc(start.sx, start.sy, 8, 0, Math.PI * 2);
      ctx.stroke();
      ctx.fillStyle = "rgba(14, 165, 233, 0.96)";
      ctx.font = "600 13px Arial";
      ctx.fillText("START", start.sx + 10, start.sy - 9);
    }

    if (trailUntilNow.length >= 2) {
      ctx.strokeStyle = "rgba(168, 85, 247, 0.92)";
      ctx.lineWidth = Math.max(2.2, Math.min(4.5, 3 * Math.sqrt(zoom)));
      ctx.lineJoin = "round";
      ctx.lineCap = "round";
      ctx.beginPath();
      trailUntilNow.forEach((point, index) => {
        const screen = toScreenMeters(point.x, point.y);
        if (index === 0) ctx.moveTo(screen.sx, screen.sy);
        else ctx.lineTo(screen.sx, screen.sy);
      });
      ctx.stroke();
    }

    const preparedScreenDetections = [];
    const detectionsToDraw = mapPointsMode === "frames" && expandedFrameKey
      ? visibleDetections.filter((detection) => detectionFrameKey(detection) === expandedFrameKey)
      : visibleDetections;

    if (mapPointsMode === "frames") {
      for (const frame of frameGroups) {
        if (!frame.projection) continue;
        const screen = toScreenMeters(frame.projection.x, frame.projection.y);
        const isExpanded = frame.id === expandedFrameKey;
        const radius = isExpanded ? 10 : clamp(5 + Math.sqrt(frame.count) * 1.6, 7, 15);
        const color = detectionColor(frame);
        const weakLikeFrame = isWeakLikeStatus(frame.policyStatus);

        ctx.save();
        ctx.globalAlpha = weakLikeFrame ? 0.78 : 1;
        ctx.fillStyle = color;
        ctx.strokeStyle = isExpanded ? "rgba(250, 204, 21, 0.92)" : "rgba(226, 232, 240, 0.9)";
        ctx.lineWidth = isExpanded ? 3 : 1.8;
        if (weakLikeFrame && !isExpanded) ctx.setLineDash([3, 2.4]);
        ctx.beginPath();
        ctx.arc(screen.sx, screen.sy, radius, 0, Math.PI * 2);
        ctx.fill();
        ctx.stroke();
        ctx.restore();

        ctx.fillStyle = "rgba(2, 6, 23, 0.9)";
        ctx.font = "700 10px Arial";
        ctx.textAlign = "center";
        ctx.textBaseline = "middle";
        ctx.fillText(String(frame.count), screen.sx, screen.sy);
        ctx.textAlign = "left";
        ctx.textBaseline = "alphabetic";

        preparedScreenDetections.push({
          kind: "frame",
          frameKey: frame.id,
          sx: screen.sx,
          sy: screen.sy,
          radius: radius + 10,
          frame,
        });
      }
    }

    if (mapPointsMode === "objects" || expandedFrameKey) {
      for (const detection of detectionsToDraw) {
        const screen = toScreenMeters(detection.projection.x, detection.projection.y);
        const isSelected = detection.id === selectedDetectionId;
        const status = policyStatusForDetection(detection);
        const weakLike = isWeakLikeStatus(status);
        const radius = isSelected ? 8.5 : status === "heuristic" ? 7.2 : weakLike ? 4.8 : 6.2;
        const color = detectionColor(detection);

        ctx.save();
        ctx.globalAlpha = weakLike ? 0.72 : 1;
        ctx.fillStyle = color;
        ctx.strokeStyle = isSelected ? "rgba(255, 255, 255, 0.96)" : weakLike ? "rgba(226, 232, 240, 0.82)" : "rgba(15, 23, 42, 0.75)";
        ctx.lineWidth = isSelected ? 3 : weakLike ? 1.2 : status === "heuristic" ? 2.2 : 1.5;
        if (weakLike && !isSelected) ctx.setLineDash([2.5, 2]);
        ctx.beginPath();
        ctx.arc(screen.sx, screen.sy, radius, 0, Math.PI * 2);
        ctx.fill();
        ctx.stroke();
        ctx.restore();

        if (isSelected) {
          ctx.strokeStyle = "rgba(250, 204, 21, 0.85)";
          ctx.lineWidth = 2;
          ctx.beginPath();
          ctx.arc(screen.sx, screen.sy, radius + 5, 0, Math.PI * 2);
          ctx.stroke();
        }

        preparedScreenDetections.push({ kind: "detection", ...detection, sx: screen.sx, sy: screen.sy, radius: radius + 9 });
      }
    }

    if (focusDetection?.projection) {
      const focus = toScreenMeters(focusDetection.projection.x, focusDetection.projection.y);
      ctx.fillStyle = "rgba(168, 85, 247, 0.95)";
      ctx.strokeStyle = "rgba(255, 255, 255, 0.95)";
      ctx.lineWidth = 2.5;
      ctx.beginPath();
      ctx.arc(focus.sx, focus.sy, 8.5, 0, Math.PI * 2);
      ctx.fill();
      ctx.stroke();

      ctx.strokeStyle = "rgba(216, 180, 254, 0.95)";
      ctx.lineWidth = 2;
      ctx.beginPath();
      ctx.arc(focus.sx, focus.sy, 16, 0, Math.PI * 2);
      ctx.stroke();

      ctx.fillStyle = "rgba(216, 180, 254, 0.95)";
      ctx.font = "700 12px Arial";
      ctx.fillText("REVIEW", focus.sx + 14, focus.sy - 10);
    }

    if (finalPose) {
      drawRobot2D(ctx, toScreenMeters(finalPose.x, finalPose.y), finalPose.yawDeg);
    }

    ctx.strokeStyle = "rgba(148, 163, 184, 0.25)";
    ctx.lineWidth = 1;
    ctx.strokeRect(offsetX, offsetY, drawWidth, drawHeight);
    ctx.restore();
    setScreenDetections(preparedScreenDetections);
  }

  function drawRobot2D(ctx, robot, yawDeg) {
    const yawRad = deg2rad(yawDeg ?? 0);
    const size = 20;
    const tip = {
      x: robot.sx + Math.sin(yawRad) * size,
      y: robot.sy - Math.cos(yawRad) * size,
    };
    const leftWing = {
      x: robot.sx + Math.sin(yawRad + deg2rad(140)) * (size * 0.78),
      y: robot.sy - Math.cos(yawRad + deg2rad(140)) * (size * 0.78),
    };
    const rightWing = {
      x: robot.sx + Math.sin(yawRad - deg2rad(140)) * (size * 0.78),
      y: robot.sy - Math.cos(yawRad - deg2rad(140)) * (size * 0.78),
    };

    ctx.fillStyle = "rgba(14, 165, 233, 0.16)";
    ctx.strokeStyle = "rgba(56, 189, 248, 0.92)";
    ctx.lineWidth = 2.5;
    ctx.beginPath();
    ctx.arc(robot.sx, robot.sy, 22, 0, Math.PI * 2);
    ctx.fill();
    ctx.stroke();

    ctx.fillStyle = "rgba(226, 232, 240, 0.98)";
    ctx.strokeStyle = "rgba(2, 6, 23, 0.92)";
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(tip.x, tip.y);
    ctx.lineTo(leftWing.x, leftWing.y);
    ctx.lineTo(rightWing.x, rightWing.y);
    ctx.closePath();
    ctx.fill();
    ctx.stroke();

    ctx.fillStyle = "rgba(226, 232, 240, 0.92)";
    ctx.font = "600 12px Arial";
    ctx.fillText("ROBOT", robot.sx + 18, robot.sy + 3);
  }

  function drawRowWall(ctx, project, side) {
    const trail = sampledTrail(trailUntilNow, 90);
    if (trail.length < 2) return;

    const offsetPoints = trail
      .map((pose) => {
        const normal = sideNormalFromYaw(pose.yawDeg, side);
        return {
          x: pose.x + normal.x * ROW_DISTANCE_M,
          y: pose.y + normal.y * ROW_DISTANCE_M,
        };
      })
      .filter((point) => Number.isFinite(point.x) && Number.isFinite(point.y));

    if (offsetPoints.length < 2) return;

    const topPoints = offsetPoints.map((point) => project(point.x, point.y, PLANT_MAX_HEIGHT_M));
    const bottomPoints = offsetPoints.map((point) => project(point.x, point.y, PLANT_MIN_HEIGHT_M));

    ctx.save();
    ctx.fillStyle = side === "left" ? "rgba(14, 165, 233, 0.055)" : "rgba(168, 85, 247, 0.055)";
    ctx.strokeStyle = side === "left" ? "rgba(14, 165, 233, 0.22)" : "rgba(168, 85, 247, 0.22)";
    ctx.lineWidth = 1.2;
    ctx.beginPath();
    topPoints.forEach((point, index) => {
      if (index === 0) ctx.moveTo(point.sx, point.sy);
      else ctx.lineTo(point.sx, point.sy);
    });
    [...bottomPoints].reverse().forEach((point) => ctx.lineTo(point.sx, point.sy));
    ctx.closePath();
    ctx.fill();
    ctx.stroke();

    ctx.strokeStyle = "rgba(226, 232, 240, 0.12)";
    ctx.setLineDash([4, 5]);
    const guideStep = Math.max(1, Math.floor(offsetPoints.length / 10));
    for (let index = 0; index < offsetPoints.length; index += guideStep) {
      const point = offsetPoints[index];
      const bottom = project(point.x, point.y, PLANT_MIN_HEIGHT_M);
      const top = project(point.x, point.y, PLANT_MAX_HEIGHT_M);
      ctx.beginPath();
      ctx.moveTo(bottom.sx, bottom.sy);
      ctx.lineTo(top.sx, top.sy);
      ctx.stroke();
    }
    ctx.restore();
  }

  function drawRow3DMap(ctx, cssWidth, cssHeight) {
    const { scale, verticalScale, project } = getRow3DMetrics();
    const preparedScreenDetections = [];

    const mapCanvas = makeMapImageCanvas(map, mapWidthPx, mapHeightPx);
    if (mapCanvas) {
      const p00 = project(0, 0, 0);
      const p10 = project(resolution, 0, 0);
      const p01 = project(0, resolution, 0);
      ctx.save();
      ctx.globalAlpha = 0.7;
      ctx.imageSmoothingEnabled = false;
      ctx.transform(
        p10.sx - p00.sx,
        p10.sy - p00.sy,
        p01.sx - p00.sx,
        p01.sy - p00.sy,
        p00.sx,
        p00.sy,
      );
      ctx.drawImage(mapCanvas, 0, 0, mapWidthPx, mapHeightPx);
      ctx.restore();
      ctx.imageSmoothingEnabled = true;
    } else {
      const corners = [project(0, 0, 0), project(mapWidthM, 0, 0), project(mapWidthM, mapHeightM, 0), project(0, mapHeightM, 0)];
      ctx.fillStyle = "rgba(30, 41, 59, 0.72)";
      ctx.beginPath();
      corners.forEach((corner, index) => {
        if (index === 0) ctx.moveTo(corner.sx, corner.sy);
        else ctx.lineTo(corner.sx, corner.sy);
      });
      ctx.closePath();
      ctx.fill();
    }

    ctx.strokeStyle = "rgba(148, 163, 184, 0.18)";
    ctx.lineWidth = 1;
    const gridStepMeters = 0.5;
    for (let x = 0; x <= mapWidthM; x += gridStepMeters) {
      const a = project(x, 0, 0);
      const b = project(x, mapHeightM, 0);
      ctx.beginPath();
      ctx.moveTo(a.sx, a.sy);
      ctx.lineTo(b.sx, b.sy);
      ctx.stroke();
    }
    for (let y = 0; y <= mapHeightM; y += gridStepMeters) {
      const a = project(0, y, 0);
      const b = project(mapWidthM, y, 0);
      ctx.beginPath();
      ctx.moveTo(a.sx, a.sy);
      ctx.lineTo(b.sx, b.sy);
      ctx.stroke();
    }

    if (rowSideCounts.left > 0) drawRowWall(ctx, project, "left");
    if (rowSideCounts.right > 0) drawRowWall(ctx, project, "right");

    ctx.save();
    ctx.strokeStyle = "rgba(168, 85, 247, 0.95)";
    ctx.lineWidth = Math.max(2.4, Math.min(4.8, 3.2 * Math.sqrt(zoom)));
    ctx.lineJoin = "round";
    ctx.lineCap = "round";
    if (trailUntilNow.length >= 2) {
      ctx.beginPath();
      trailUntilNow.forEach((point, index) => {
        const screen = project(point.x, point.y, 0.035);
        if (index === 0) ctx.moveTo(screen.sx, screen.sy);
        else ctx.lineTo(screen.sx, screen.sy);
      });
      ctx.stroke();
    }
    ctx.restore();

    const drawFrameGroups = [...row3DFrameGroups].sort((a, b) => {
      const pa = project(a.row3d.x, a.row3d.y, a.row3d.z);
      const pb = project(b.row3d.x, b.row3d.y, b.row3d.z);
      return pa.sy - pb.sy;
    });

    if (mapPointsMode === "frames") {
      for (const frame of drawFrameGroups) {
        const isExpanded = frame.id === expandedFrameKey;
        const center = project(frame.row3d.x, frame.row3d.y, frame.row3d.z);
        const lengthM = clamp(0.18 + frame.count * 0.012, 0.24, 0.78);
        const halfLength = lengthM / 2;
        const forward = frame.row3d.forward ?? { x: 0, y: -1 };
        const a = project(frame.row3d.x - forward.x * halfLength, frame.row3d.y - forward.y * halfLength, frame.row3d.z);
        const b = project(frame.row3d.x + forward.x * halfLength, frame.row3d.y + forward.y * halfLength, frame.row3d.z);
        const color = detectionColor(frame);
        const weakLikeFrame = isWeakLikeStatus(frame.policyStatus);
        const lineWidth = isExpanded ? 9 : clamp(4 + Math.sqrt(frame.count) * 1.2, 6, 13);

        ctx.save();
        ctx.globalAlpha = weakLikeFrame ? 0.82 : 1;
        ctx.strokeStyle = color;
        ctx.lineWidth = lineWidth;
        ctx.lineCap = "round";
        if (weakLikeFrame && !isExpanded) ctx.setLineDash([5, 3]);
        ctx.beginPath();
        ctx.moveTo(a.sx, a.sy);
        ctx.lineTo(b.sx, b.sy);
        ctx.stroke();
        ctx.restore();

        ctx.save();
        ctx.strokeStyle = isExpanded ? "rgba(250, 204, 21, 0.95)" : "rgba(226, 232, 240, 0.85)";
        ctx.lineWidth = isExpanded ? 3 : 1.5;
        ctx.beginPath();
        ctx.arc(center.sx, center.sy, lineWidth + 4, 0, Math.PI * 2);
        ctx.stroke();
        ctx.restore();

        ctx.save();
        ctx.fillStyle = "rgba(2, 6, 23, 0.9)";
        ctx.strokeStyle = "rgba(226, 232, 240, 0.75)";
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.arc(center.sx, center.sy, Math.max(9, lineWidth + 1), 0, Math.PI * 2);
        ctx.fill();
        ctx.stroke();
        ctx.fillStyle = "rgba(226, 232, 240, 0.96)";
        ctx.font = "800 10px Arial";
        ctx.textAlign = "center";
        ctx.textBaseline = "middle";
        ctx.fillText(String(frame.count), center.sx, center.sy);
        ctx.restore();

        preparedScreenDetections.push({
          kind: "frame",
          frameKey: frame.id,
          sx: center.sx,
          sy: center.sy,
          radius: Math.max(18, lineWidth + 12),
          frame,
        });
      }
    }

    const drawDetections = (mapPointsMode === "objects" ? row3DDetections : expandedRow3DDetections).sort((a, b) => {
      const pa = project(a.row3d.x, a.row3d.y, a.row3d.z);
      const pb = project(b.row3d.x, b.row3d.y, b.row3d.z);
      return pa.sy - pb.sy;
    });

    if (mapPointsMode === "objects" || expandedFrameKey) {
      for (const detection of drawDetections) {
        const point = project(detection.row3d.x, detection.row3d.y, detection.row3d.z);
        const foot = project(detection.row3d.x, detection.row3d.y, PLANT_MIN_HEIGHT_M);
        const isSelected = detection.id === selectedDetectionId;
        const status = policyStatusForDetection(detection);
        const weakLike = isWeakLikeStatus(status);
        const radius = isSelected ? 8.8 : status === "heuristic" ? 7.4 : weakLike ? 4.7 : 6.2;
        const color = detectionColor(detection);

        ctx.save();
        ctx.globalAlpha = weakLike ? 0.72 : 1;
        ctx.strokeStyle = "rgba(226, 232, 240, 0.22)";
        ctx.lineWidth = 1;
        ctx.setLineDash([3, 5]);
        ctx.beginPath();
        ctx.moveTo(foot.sx, foot.sy);
        ctx.lineTo(point.sx, point.sy);
        ctx.stroke();
        ctx.restore();

        ctx.save();
        ctx.globalAlpha = weakLike ? 0.74 : 1;
        ctx.fillStyle = color;
        ctx.strokeStyle = isSelected ? "rgba(255, 255, 255, 0.98)" : weakLike ? "rgba(226, 232, 240, 0.82)" : "rgba(2, 6, 23, 0.8)";
        ctx.lineWidth = isSelected ? 3 : weakLike ? 1.15 : status === "heuristic" ? 2.1 : 1.55;
        if (weakLike && !isSelected) ctx.setLineDash([2.5, 2]);
        ctx.beginPath();
        ctx.arc(point.sx, point.sy, radius, 0, Math.PI * 2);
        ctx.fill();
        ctx.stroke();
        ctx.restore();

        if (isSelected) {
          ctx.strokeStyle = "rgba(250, 204, 21, 0.9)";
          ctx.lineWidth = 2;
          ctx.beginPath();
          ctx.arc(point.sx, point.sy, radius + 5, 0, Math.PI * 2);
          ctx.stroke();
        }

        preparedScreenDetections.push({ kind: "detection", ...detection, sx: point.sx, sy: point.sy, radius: radius + 9 });
      }
    }

    if (focusDetection?.projection) {
      const focusRow = buildRow3DPoint(focusDetection);
      if (focusRow) {
        const focus = project(focusRow.x, focusRow.y, focusRow.z);
        ctx.fillStyle = "rgba(168, 85, 247, 0.95)";
        ctx.strokeStyle = "rgba(255, 255, 255, 0.95)";
        ctx.lineWidth = 2.5;
        ctx.beginPath();
        ctx.arc(focus.sx, focus.sy, 8.5, 0, Math.PI * 2);
        ctx.fill();
        ctx.stroke();

        ctx.strokeStyle = "rgba(216, 180, 254, 0.95)";
        ctx.lineWidth = 2;
        ctx.beginPath();
        ctx.arc(focus.sx, focus.sy, 16, 0, Math.PI * 2);
        ctx.stroke();
      }
    }

    if (finalPose) {
      const robot = project(finalPose.x, finalPose.y, 0.08);
      drawRobot3D(ctx, robot, finalPose.yawDeg, scale, verticalScale);
    }

    ctx.fillStyle = "rgba(226, 232, 240, 0.75)";
    ctx.font = "700 11px Arial";
    ctx.fillText(`Plant height: ${PLANT_MIN_HEIGHT_M.toFixed(1)}–${PLANT_MAX_HEIGHT_M.toFixed(1)}m`, 18, cssHeight - 24);
    ctx.fillText(`Servo side inference: L ${rowSideCounts.left} / R ${rowSideCounts.right}`, 18, cssHeight - 9);

    setScreenDetections(preparedScreenDetections);
  }

  function drawRobot3D(ctx, robot, yawDeg, scale, verticalScale) {
    const yawRad = deg2rad(yawDeg ?? 0);
    const size = Math.max(14, Math.min(24, scale * 0.35));
    const tip = {
      x: robot.sx + Math.sin(yawRad) * size,
      y: robot.sy - Math.cos(yawRad) * size * 0.55,
    };
    const leftWing = {
      x: robot.sx + Math.sin(yawRad + deg2rad(140)) * (size * 0.78),
      y: robot.sy - Math.cos(yawRad + deg2rad(140)) * (size * 0.44),
    };
    const rightWing = {
      x: robot.sx + Math.sin(yawRad - deg2rad(140)) * (size * 0.78),
      y: robot.sy - Math.cos(yawRad - deg2rad(140)) * (size * 0.44),
    };

    ctx.save();
    ctx.fillStyle = "rgba(14, 165, 233, 0.18)";
    ctx.strokeStyle = "rgba(56, 189, 248, 0.95)";
    ctx.lineWidth = 2.5;
    ctx.beginPath();
    ctx.ellipse(robot.sx, robot.sy, size * 1.3, size * 0.72, 0, 0, Math.PI * 2);
    ctx.fill();
    ctx.stroke();

    ctx.strokeStyle = "rgba(56, 189, 248, 0.75)";
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    ctx.moveTo(robot.sx, robot.sy);
    ctx.lineTo(robot.sx, robot.sy - Math.min(28, verticalScale * 0.35));
    ctx.stroke();

    ctx.fillStyle = "rgba(226, 232, 240, 0.98)";
    ctx.strokeStyle = "rgba(2, 6, 23, 0.92)";
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(tip.x, tip.y);
    ctx.lineTo(leftWing.x, leftWing.y);
    ctx.lineTo(rightWing.x, rightWing.y);
    ctx.closePath();
    ctx.fill();
    ctx.stroke();
    ctx.restore();
  }

  useEffect(() => {
    if (!isRosMap) return;
    const canvas = canvasRef.current;
    if (!canvas) return;

    const ctx = canvas.getContext("2d");
    const dpr = window.devicePixelRatio || 1;
    const { cssWidth, cssHeight } = viewMode === "row3d" ? getRow3DMetrics() : getViewMetrics();

    canvas.width = Math.floor(cssWidth * dpr);
    canvas.height = Math.floor(cssHeight * dpr);
    canvas.style.width = `${cssWidth}px`;
    canvas.style.height = `${cssHeight}px`;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, cssWidth, cssHeight);

    ctx.fillStyle = "rgba(2, 6, 23, 0.94)";
    ctx.fillRect(0, 0, cssWidth, cssHeight);

    if (viewMode === "row3d") {
      drawRow3DMap(ctx, cssWidth, cssHeight);
    } else {
      draw2DMap(ctx, cssWidth, cssHeight);
    }
  }, [
    isRosMap,
    map,
    canvasSize,
    height,
    mapWidthPx,
    mapHeightPx,
    mapWidthM,
    mapHeightM,
    resolution,
    trailUntilNow,
    visibleDetections,
    row3DDetections,
    row3DFrameGroups,
    expandedRow3DDetections,
    frameGroups,
    rowSideCounts,
    mapPointsMode,
    expandedFrameKey,
    selectedDetectionId,
    focusDetection,
    finalPose,
    zoom,
    pan,
    viewMode,
    row3DRotationDeg,
  ]);

  function zoomAt(canvasX, canvasY, nextZoom) {
    const boundedZoom = clamp(nextZoom, 1, viewMode === "row3d" ? 3.5 : 5);
    if (viewMode === "row3d") {
      setZoom(boundedZoom);
      return;
    }

    const oldMetrics = getViewMetrics(zoom, pan);
    const mapPxX = (canvasX - oldMetrics.offsetX) / oldMetrics.scale;
    const mapPxY = (canvasY - oldMetrics.offsetY) / oldMetrics.scale;
    const newDrawWidth = mapWidthPx * oldMetrics.fitScale * boundedZoom;
    const newDrawHeight = mapHeightPx * oldMetrics.fitScale * boundedZoom;
    const baseOffsetX = (oldMetrics.cssWidth - newDrawWidth) / 2;
    const baseOffsetY = (oldMetrics.cssHeight - newDrawHeight) / 2;
    const nextPan = {
      x: canvasX - mapPxX * oldMetrics.fitScale * boundedZoom - baseOffsetX,
      y: canvasY - mapPxY * oldMetrics.fitScale * boundedZoom - baseOffsetY,
    };
    setZoom(boundedZoom);
    setPan(nextPan);
  }

  function changeZoom(multiplier) {
    zoomAt(canvasSize.width / 2, canvasSize.height / 2, zoom * multiplier);
  }

  function changeRow3DRotation(deltaDeg) {
    setRow3DRotationDeg((current) => {
      const next = Math.round(current + deltaDeg);
      writeRow3DRotationDeg(next);
      return next;
    });
  }

  function resetView() {
    setZoom(1);
    setPan({ x: 0, y: 0 });
    if (viewMode === "row3d") {
      setRow3DRotationDeg(0);
      writeRow3DRotationDeg(0);
    }
  }

  function changeViewMode(nextMode) {
    setViewMode(nextMode);
    writeMapViewMode(nextMode);
  }

  function changeMapPointsMode(nextMode) {
    setMapPointsMode(nextMode);
    writeMapPointsMode(nextMode);
    setExpandedFrameKey(null);
  }

  function handleMouseDown(event) {
    dragRef.current = {
      active: true,
      x: event.clientX,
      y: event.clientY,
      moved: false,
    };
  }

  function handleMouseUp() {
    dragRef.current.active = false;
  }

  function handleMouseLeave() {
    dragRef.current.active = false;
    const canvas = canvasRef.current;
    if (canvas) canvas.style.cursor = "grab";
  }

  function handleCanvasClick(event) {
    if (dragRef.current.moved) {
      dragRef.current.moved = false;
      return;
    }

    if (!onSelectDetection || !screenDetections.length) return;
    const canvas = canvasRef.current;
    const rect = canvas.getBoundingClientRect();
    const x = event.clientX - rect.left;
    const y = event.clientY - rect.top;

    let best = null;
    let bestDist = Infinity;
    for (const detection of screenDetections) {
      const dist = Math.hypot(detection.sx - x, detection.sy - y);
      if (dist <= detection.radius && dist < bestDist) {
        best = detection;
        bestDist = dist;
      }
    }

    if (best?.kind === "frame") {
      setExpandedFrameKey((current) => (current === best.frameKey ? null : best.frameKey));
      return;
    }

    if (best) onSelectDetection(best);
  }

  function handleMouseMove(event) {
    const canvas = canvasRef.current;
    if (!canvas) return;

    if (dragRef.current.active) {
      const dx = event.clientX - dragRef.current.x;
      const dy = event.clientY - dragRef.current.y;
      if (Math.abs(dx) + Math.abs(dy) > 2) dragRef.current.moved = true;
      dragRef.current.x = event.clientX;
      dragRef.current.y = event.clientY;
      setPan((current) => ({ x: current.x + dx, y: current.y + dy }));
      canvas.style.cursor = "grabbing";
      return;
    }

    const rect = canvas.getBoundingClientRect();
    const x = event.clientX - rect.left;
    const y = event.clientY - rect.top;
    const onPoint = screenDetections.some(
      (detection) => Math.hypot(detection.sx - x, detection.sy - y) <= detection.radius,
    );
    canvas.style.cursor = onPoint ? "pointer" : zoom > 1 || viewMode === "row3d" ? "grab" : "default";
  }

  function toggleMapHeader() {
    setMapHeaderCollapsed((current) => {
      const next = !current;
      writeMapHeaderCollapsed(next);
      return next;
    });
  }

  if (!map) {
    return (
      <section className="rounded-[2rem] border border-slate-800 bg-slate-950/70 p-6 text-sm text-slate-300">
        Loading ROS2 SLAM map and tomato detections...
      </section>
    );
  }

  if (!isRosMap) {
    return (
      <section className="rounded-[2rem] border border-slate-800 bg-slate-950/70 p-6 text-sm text-amber-200">
        This dashboard view expects a ROS2 SLAM session map payload.
      </section>
    );
  }

  const mapModeLabel = viewMode === "row3d" ? "3D row view" : "2D SLAM map";
  const pointModeLabel = mapPointsMode === "frames" ? "Frame points" : "Object points";
  const pendingLabel = mapPointsMode === "frames"
    ? `${frameGroups.length} frames / ${visibleDetections.length} objects`
    : `${visibleDetections.length} objects`;

  return (
    <section className="overflow-hidden rounded-[2rem] border border-slate-800 bg-slate-950/70 shadow-[0_24px_80px_rgba(2,6,23,0.35)]">
      {mapHeaderCollapsed ? (
        <div className="flex flex-wrap items-center justify-between gap-2 border-b border-slate-800/70 px-4 py-2">
          <div className="flex min-w-0 flex-wrap items-center gap-2 text-[11px] text-slate-400">
            <span className="font-semibold uppercase tracking-[0.24em] text-cyan-300">Scan map</span>
            <span className="rounded-full border border-slate-700 bg-slate-900/70 px-2.5 py-1 text-slate-300">
              {mapModeLabel}
            </span>
            <span className="rounded-full border border-slate-700 bg-slate-900/70 px-2.5 py-1 text-slate-300">
              {pointModeLabel}
            </span>
            <span className="rounded-full border border-slate-700 bg-slate-900/70 px-2.5 py-1 text-slate-300">
              Trail {trailUntilNow.length}/{map.trail?.length ?? 0}
            </span>
            <span className="rounded-full border border-slate-700 bg-slate-900/70 px-2.5 py-1 text-slate-300">
              Pending {pendingLabel}
            </span>
            <span className="rounded-full border border-slate-700 bg-slate-900/70 px-2.5 py-1 text-slate-300">
              {formatNumber(finalPose?.distanceM ?? 0)} m
            </span>
          </div>
          <button
            type="button"
            onClick={toggleMapHeader}
            className="rounded-full border border-cyan-400/30 bg-cyan-400/10 px-3 py-1 text-[10px] font-bold uppercase tracking-[0.12em] text-cyan-100 transition hover:bg-cyan-400/15"
            title="Open map information header"
          >
            Map info
          </button>
        </div>
      ) : (
        <div className="flex flex-wrap items-start justify-between gap-3 border-b border-slate-800/90 px-5 py-4">
          <div>
            <div className="text-[11px] font-semibold uppercase tracking-[0.28em] text-cyan-300">
              Scan Map
            </div>
            <h2 className="mt-1 text-xl font-semibold text-white">ROS2 SLAM map overlay</h2>
            <p className="mt-1 text-xs text-slate-500">
              3D Row View projects detections onto a plant plane by servo pan side and bbox height.
            </p>
          </div>
          <div className="flex flex-wrap items-center justify-end gap-2 text-xs text-slate-300">
            <span className="rounded-full border border-slate-700 bg-slate-900/80 px-3 py-1">
              Trail {trailUntilNow.length}/{map.trail?.length ?? 0}
            </span>
            <span className="rounded-full border border-slate-700 bg-slate-900/80 px-3 py-1">
              Pending map points {pendingLabel}
            </span>
            <span className="rounded-full border border-slate-700 bg-slate-900/80 px-3 py-1">
              Distance {formatNumber(finalPose?.distanceM ?? 0)} m
            </span>
            <button
              type="button"
              onClick={toggleMapHeader}
              className="flex h-5 w-5 items-center justify-center rounded-full border border-red-200/70 bg-red-600 text-[13px] font-black leading-none text-white shadow-[0_0_16px_rgba(220,38,38,0.35)] transition hover:scale-105 hover:bg-red-500"
              aria-label="Collapse map information header"
              title="Minimize map information header"
            >
              −
            </button>
          </div>
        </div>
      )}

      <div className="flex flex-wrap items-center justify-between gap-2 border-b border-slate-800/60 px-5 py-3 text-xs text-slate-400">
        <div className="flex flex-wrap items-center gap-2">
          <div className="mr-1 flex rounded-xl border border-slate-700 bg-slate-950 p-1">
            <button
              type="button"
              onClick={() => changeViewMode("map2d")}
              className={`rounded-lg px-3 py-1.5 font-semibold transition ${
                viewMode === "map2d" ? "bg-cyan-400/20 text-cyan-100" : "text-slate-400 hover:bg-slate-900 hover:text-slate-200"
              }`}
            >
              2D map
            </button>
            <button
              type="button"
              onClick={() => changeViewMode("row3d")}
              className={`rounded-lg px-3 py-1.5 font-semibold transition ${
                viewMode === "row3d" ? "bg-cyan-400/20 text-cyan-100" : "text-slate-400 hover:bg-slate-900 hover:text-slate-200"
              }`}
            >
              3D row
            </button>
          </div>
          <div className="mr-1 flex rounded-xl border border-slate-700 bg-slate-950 p-1">
            <button
              type="button"
              onClick={() => changeMapPointsMode("frames")}
              className={`rounded-lg px-3 py-1.5 font-semibold transition ${
                mapPointsMode === "frames" ? "bg-amber-400/20 text-amber-100" : "text-slate-400 hover:bg-slate-900 hover:text-slate-200"
              }`}
              title="Show one marker per frame, then click a frame to reveal its detections"
            >
              Frame points
            </button>
            <button
              type="button"
              onClick={() => changeMapPointsMode("objects")}
              className={`rounded-lg px-3 py-1.5 font-semibold transition ${
                mapPointsMode === "objects" ? "bg-amber-400/20 text-amber-100" : "text-slate-400 hover:bg-slate-900 hover:text-slate-200"
              }`}
              title="Show every detection object as an independent marker"
            >
              Object points
            </button>
          </div>
          <button type="button" onClick={() => changeZoom(1.2)} className="rounded-xl border border-slate-700 bg-slate-900 px-3 py-1.5 font-semibold text-slate-200 hover:bg-slate-800">Zoom +</button>
          <button type="button" onClick={() => changeZoom(1 / 1.2)} className="rounded-xl border border-slate-700 bg-slate-900 px-3 py-1.5 font-semibold text-slate-200 hover:bg-slate-800">Zoom -</button>
          {viewMode === "row3d" ? (
            <>
              <button type="button" onClick={() => changeRow3DRotation(-12)} className="rounded-xl border border-purple-400/25 bg-purple-400/10 px-3 py-1.5 font-semibold text-purple-100 hover:bg-purple-400/15">Rotate ←</button>
              <button type="button" onClick={() => changeRow3DRotation(12)} className="rounded-xl border border-purple-400/25 bg-purple-400/10 px-3 py-1.5 font-semibold text-purple-100 hover:bg-purple-400/15">Rotate →</button>
            </>
          ) : null}
          <button type="button" onClick={resetView} className="rounded-xl border border-slate-700 bg-slate-900 px-3 py-1.5 font-semibold text-slate-200 hover:bg-slate-800">Reset</button>
        </div>
        <div className="flex flex-wrap items-center gap-2">
          {expandedFrameKey ? (
            <button
              type="button"
              onClick={() => setExpandedFrameKey(null)}
              className="rounded-full border border-amber-400/25 bg-amber-400/10 px-3 py-1 text-amber-100 hover:bg-amber-400/15"
              title="Close expanded frame detections"
            >
              Expanded frame · {expandedFrameKey.split("|").at(-1)?.slice(-34) || "selected"} ×
            </button>
          ) : null}
          {viewMode === "row3d" ? (
            <div className="rounded-full border border-purple-400/20 bg-purple-400/10 px-3 py-1 text-purple-100">
              Height {PLANT_MIN_HEIGHT_M.toFixed(1)}–{PLANT_MAX_HEIGHT_M.toFixed(1)}m · L {rowSideCounts.left} / R {rowSideCounts.right} · Rotate {row3DRotationDeg}°
            </div>
          ) : null}
          <div className="rounded-full border border-cyan-400/20 bg-cyan-400/10 px-3 py-1 text-cyan-100">
            Zoom {Math.round(zoom * 100)}%
          </div>
        </div>
      </div>

      <div ref={wrapperRef} className="p-4">
        <canvas
          ref={canvasRef}
          onClick={handleCanvasClick}
          onMouseDown={handleMouseDown}
          onMouseUp={handleMouseUp}
          onMouseLeave={handleMouseLeave}
          onMouseMove={handleMouseMove}
          className="block w-full select-none rounded-3xl border border-slate-800 bg-slate-950"
          aria-label="Robot scan map with route and tomato detections"
        />
      </div>

      <div className="grid gap-3 border-t border-slate-800/90 px-5 py-4 text-xs text-slate-400 md:grid-cols-4 lg:grid-cols-8">
        <div className="flex items-center gap-2">
          <span className="h-3 w-3 rounded-full bg-red-500" /> Strong ripe tomato
        </div>
        <div className="flex items-center gap-2">
          <span className="h-3 w-3 rounded-full bg-green-500" /> Strong unripe tomato
        </div>
        <div className="flex items-center gap-2">
          <span className="h-3 w-3 rounded-full border border-purple-500 bg-purple-950" /> Strong ripe bunch
        </div>
        <div className="flex items-center gap-2">
          <span className="h-3 w-3 rounded-full bg-orange-500" /> Strong unripe bunch
        </div>
        <div className="flex items-center gap-2">
          <span className="h-3 w-3 rounded-full bg-yellow-300" /> Heuristics
        </div>
        <div className="flex items-center gap-2">
          <span className="h-3 w-3 rounded-full bg-amber-400" /> Review
        </div>
        <div className="flex items-center gap-2">
          <span className="h-3 w-3 rounded-full bg-fuchsia-400" /> Color corrected
        </div>
        <div className="flex items-center gap-2">
          <span className="h-3 w-3 rounded-full bg-gradient-to-r from-red-500/55 via-green-500/55 to-orange-500/55 ring-1 ring-white/20" /> Weak = class color + transparent · Noise gray
        </div>
      </div>
    </section>
  );
}
