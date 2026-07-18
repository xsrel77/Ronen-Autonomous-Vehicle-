import os from "node:os";
import path from "node:path";
import { createReadStream, existsSync } from "node:fs";
import { promises as fs } from "node:fs";
import readline from "node:readline";

const LEGACY_DEBUG_DIR = path.join(
  os.homedir(),
  "Desktop/robot_rco_farm/robot_rco_farm-client/robot_rco_farm-client/src/filetestjson",
);

function resolveDebugDir() {
  const configuredDir = process.env.ROBOT_DEBUG_DIR;
  const candidates = configuredDir
    ? [configuredDir]
    : [path.join(process.cwd(), "src", "filetestjson"), LEGACY_DEBUG_DIR];

  const existingDir = candidates.find((candidate) => existsSync(candidate));
  return existingDir ?? candidates[0];
}

const DEBUG_DIR = resolveDebugDir();
const LATEST_PATH = path.join(DEBUG_DIR, "to_client_latest.json");
const LOG_PATH = path.join(DEBUG_DIR, "to_client_debug_log.jsonl");
const STREAM_STEP_MS = 250;
const HISTORY_WINDOW = 18;
const MAX_REPLAY_ENTRIES = 1600;
const ANALYSIS_SERIES_MAX = 220;

let replayCache = null;
let latestCache = null;
let streamState = {
  startedAtMs: null,
};

function buildMissingFileError(filePath, label, cause) {
  const error = new Error(
    `Missing ${label} at ${filePath}. Set ROBOT_DEBUG_DIR to the folder that contains to_client_latest.json and to_client_debug_log.jsonl.`,
  );
  error.cause = cause;
  return error;
}

async function statRequired(filePath, label) {
  try {
    return await fs.stat(filePath);
  } catch (error) {
    throw buildMissingFileError(filePath, label, error);
  }
}

function safeNumber(value, fallback = 0) {
  const num = Number(value);
  return Number.isFinite(num) ? num : fallback;
}

function clamp01(value) {
  return Math.max(0, Math.min(1, value));
}

function average(values) {
  if (!values.length) return null;
  return values.reduce((sum, value) => sum + value, 0) / values.length;
}

function correlation(xs, ys) {
  if (xs.length !== ys.length || xs.length < 2) return null;

  const meanX = average(xs);
  const meanY = average(ys);

  let cov = 0;
  let varX = 0;
  let varY = 0;

  for (let i = 0; i < xs.length; i += 1) {
    const dx = xs[i] - meanX;
    const dy = ys[i] - meanY;
    cov += dx * dy;
    varX += dx * dx;
    varY += dy * dy;
  }

  if (varX === 0 || varY === 0) return null;
  return cov / Math.sqrt(varX * varY);
}

function summarizeMetric(values) {
  if (!values.length) {
    return { min: null, max: null, avg: null };
  }

  return {
    min: Math.min(...values),
    max: Math.max(...values),
    avg: average(values),
  };
}

function shouldKeepReplayEntry(entry, lineIndex, approxStep) {
  const pointCloudCount = entry.lidar?.snapshot?.point_cloud?.length ?? 0;
  const hasDetections = safeNumber(entry.perception?.detection_count) > 0;
  const lidarValid = Boolean(entry.lidar?.pose?.valid);
  const warning = Boolean(entry.robot?.warning_active);
  return (
    lineIndex % approxStep === 0 ||
    pointCloudCount > 0 ||
    hasDetections ||
    lidarValid ||
    warning
  );
}

function getPose(entry) {
  const navPose = entry.navigation?.pose ?? {};
  const odomPose = entry.odom?.pose ?? {};

  return {
    x: safeNumber(navPose.x_m, safeNumber(odomPose.x_m)),
    y: safeNumber(navPose.y_m, safeNumber(odomPose.y_m)),
    yawDeg: safeNumber(navPose.yaw_deg, safeNumber(odomPose.yaw_deg)),
  };
}

function rotatePoint(point, yawDeg) {
  const rad = (yawDeg * Math.PI) / 180;
  const cos = Math.cos(rad);
  const sin = Math.sin(rad);

  return {
    x: point.x_m * cos - point.y_m * sin,
    y: point.x_m * sin + point.y_m * cos,
  };
}

function toWorldPoint(localPoint, pose) {
  const rotated = rotatePoint(localPoint, pose.yawDeg);
  return {
    x: pose.x + rotated.x,
    y: pose.y + rotated.y,
    distanceM: safeNumber(
      localPoint.distance_m,
      Math.hypot(localPoint.x_m, localPoint.y_m),
    ),
    angleDeg: safeNumber(localPoint.angle_deg),
  };
}

function getCurrentStreamIndex(length) {
  if (!length) return 0;
  if (streamState.startedAtMs == null) {
    streamState.startedAtMs = Date.now();
  }

  const elapsedMs = Date.now() - streamState.startedAtMs;
  return Math.floor(elapsedMs / STREAM_STEP_MS) % length;
}

function buildStreamFrame(entries) {
  const index = getCurrentStreamIndex(entries.length);
  const current = entries[index];
  const history = [];

  for (let offset = HISTORY_WINDOW - 1; offset >= 0; offset -= 1) {
    const historyIndex = (index - offset + entries.length) % entries.length;
    history.push(entries[historyIndex]);
  }

  return { current, history, index };
}

async function streamJsonLines(filePath, onLine) {
  const stream = createReadStream(filePath, { encoding: "utf8" });
  const rl = readline.createInterface({
    input: stream,
    crlfDelay: Infinity,
  });

  let parsedLines = 0;
  let invalidLines = 0;
  let lfsPointerDetected = false;

  try {
    for await (const line of rl) {
      const trimmed = line.trim();
      if (!trimmed) continue;

      if (trimmed.startsWith("version https://git-lfs.github.com/spec/")) {
        lfsPointerDetected = true;
        invalidLines += 1;
        continue;
      }

      try {
        await onLine(JSON.parse(trimmed), parsedLines);
        parsedLines += 1;
      } catch {
        // Keep the dashboard alive even if the JSONL file contains a bad line.
        // This also protects ZIP downloads that contain a Git-LFS pointer file
        // instead of the real, large debug log.
        invalidLines += 1;
      }
    }
  } finally {
    rl.close();
    stream.destroy();
  }

  return { parsedLines, invalidLines, lfsPointerDetected };
}

async function getReplayCache() {
  const stat = await statRequired(LOG_PATH, "debug log file");
  if (replayCache && replayCache.fileUpdatedMs === stat.mtimeMs) {
    return replayCache;
  }

  const totalApproxLines = Math.max(1, Math.floor(stat.size / 4800));
  const approxStep = Math.max(1, Math.floor(totalApproxLines / MAX_REPLAY_ENTRIES));
  const entries = [];
  let totalEntries = 0;

  const parseStats = await streamJsonLines(LOG_PATH, async (entry, index) => {
    totalEntries += 1;
    if (!shouldKeepReplayEntry(entry, index, approxStep)) return;
    entries.push(entry);
    if (entries.length > MAX_REPLAY_ENTRIES) {
      entries.shift();
    }
  });

  replayCache = {
    entries,
    totalEntries,
    fileUpdatedAt: stat.mtime.toISOString(),
    fileUpdatedMs: stat.mtimeMs,
    parseStats,
  };
  streamState.startedAtMs = null;
  return replayCache;
}

async function getLatestCache() {
  const stat = await statRequired(LATEST_PATH, "latest snapshot file");
  if (latestCache && latestCache.fileUpdatedMs === stat.mtimeMs) {
    return latestCache;
  }

  latestCache = {
    snapshot: JSON.parse(await fs.readFile(LATEST_PATH, "utf8")),
    fileUpdatedAt: stat.mtime.toISOString(),
    fileUpdatedMs: stat.mtimeMs,
  };
  return latestCache;
}

export function getRobotDebugSources() {
  return {
    debugDir: DEBUG_DIR,
    latestPath: LATEST_PATH,
    logPath: LOG_PATH,
    streamStepMs: STREAM_STEP_MS,
  };
}

export async function readLatestSnapshot() {
  return getLatestCache();
}

export async function readDebugLog() {
  const stat = await statRequired(LOG_PATH, "debug log file");
  return {
    entries: [],
    fileUpdatedAt: stat.mtime.toISOString(),
    fileUpdatedMs: stat.mtimeMs,
  };
}

export async function readRealtimeDebugFrame() {
  const replay = await getReplayCache();

  if (!replay.entries.length) {
    const latest = await getLatestCache();

    return {
      current: latest.snapshot,
      history: [latest.snapshot],
      index: 0,
      fileUpdatedAt: latest.fileUpdatedAt,
      fileUpdatedMs: latest.fileUpdatedMs,
      totalEntries: 1,
      sourceEntries: replay.totalEntries,
      fallbackReason: replay.parseStats?.lfsPointerDetected
        ? "debug-log-is-git-lfs-pointer"
        : "debug-log-empty-or-invalid",
    };
  }

  const frame = buildStreamFrame(replay.entries);
  return {
    ...frame,
    fileUpdatedAt: replay.fileUpdatedAt,
    fileUpdatedMs: replay.fileUpdatedMs,
    totalEntries: replay.entries.length,
    sourceEntries: replay.totalEntries,
  };
}

export function buildTelemetryPayload({
  snapshot,
  fileUpdatedAt,
  streamIndex = 0,
  totalEntries = 1,
}) {
  const imu = snapshot.m5stick?.imu ?? {};
  const env = snapshot.m5stick?.env ?? {};
  const health = snapshot.health ?? {};
  const lidar = snapshot.lidar ?? {};
  const navigation = snapshot.navigation ?? {};
  const odom = snapshot.odom ?? {};
  const perception = snapshot.perception ?? {};

  const accel = imu.accel ?? {};
  const gyro = imu.gyro ?? {};
  const accelMagnitude = Math.sqrt(
    safeNumber(accel.x) ** 2 +
      safeNumber(accel.y) ** 2 +
      safeNumber(accel.z) ** 2,
  );
  const gyroMagnitude = Math.sqrt(
    safeNumber(gyro.x) ** 2 +
      safeNumber(gyro.y) ** 2 +
      safeNumber(gyro.z) ** 2,
  );

  const staleSystems = Object.entries(health.stale ?? {})
    .filter(([, value]) => Boolean(value))
    .map(([key]) => key);
  const freshSystems = Object.entries(health.fresh ?? {})
    .filter(([, value]) => Boolean(value))
    .map(([key]) => key);

  const validSectorDistances = Object.values(lidar.pose?.sectors_m ?? {})
    .map((value) => safeNumber(value, -1))
    .filter((value) => value > 0);
  const nearestObstacle =
    safeNumber(lidar.pose?.nearest_m, -1) > 0
      ? safeNumber(lidar.pose?.nearest_m)
      : validSectorDistances.length
        ? Math.min(...validSectorDistances)
        : null;

  const bestDetection = perception.best_detection
    ? {
        ...perception.best_detection,
        confidence_pct: Math.round(
          safeNumber(perception.best_detection.confidence) * 100,
        ),
      }
    : null;

  return {
    ts: fileUpdatedAt,
    uptimeMs: safeNumber(snapshot.timestamp_ms),
    schemaVersion: safeNumber(snapshot.schema_version, 1),
    stream: {
      index: streamIndex,
      totalEntries,
      loopProgressPct: totalEntries
        ? Math.round((streamIndex / totalEntries) * 100)
        : 0,
      stepMs: STREAM_STEP_MS,
    },
    robot: snapshot.robot ?? {},
    drive: snapshot.drive ?? {},
    health,
    env: {
      ...env,
      temperatureC: safeNumber(env.temp_c),
      humidityPct: safeNumber(env.humidity_pct),
      pressureHpa: safeNumber(env.pressure_hpa),
      gasKohm: safeNumber(env.gas_kohm),
    },
    imu: {
      ...imu,
      accelMagnitude,
      gyroMagnitude,
    },
    lidar,
    navigation,
    odom,
    perception: {
      ...perception,
      best_detection: bestDetection,
    },
    behavior: snapshot.behavior ?? {},
    navGuard: snapshot.nav_guard ?? {},
    derived: {
      staleSystems,
      freshSystems,
      nearestObstacleM: nearestObstacle,
      lidarConfidencePct: Math.round(
        clamp01(safeNumber(lidar.pose?.confidence)) * 100,
      ),
      targetLabel: bestDetection?.label ?? null,
      targetConfidencePct: bestDetection?.confidence_pct ?? null,
      poseAgeState:
        navigation.valid && navigation.fresh
          ? "fresh"
          : navigation.stale
            ? "stale"
            : "unknown",
      statusTone: snapshot.robot?.emergency_stop
        ? "danger"
        : snapshot.robot?.warning_active
          ? "warning"
          : "success",
      pointCloudCount: Array.isArray(lidar.snapshot?.point_cloud)
        ? lidar.snapshot.point_cloud.length
        : 0,
    },
  };
}

export function buildDetectionsPayload({ snapshot, fileUpdatedAt }) {
  const detections = snapshot.perception?.detections ?? [];
  const tracking = snapshot.perception?.tracking ?? {};
  const frame = snapshot.perception?.frame ?? {};

  return detections.map((item, index) => ({
    id: `${safeNumber(snapshot.timestamp_ms)}-${index}`,
    label: item.label ?? "unknown",
    classId: safeNumber(item.class_id, -1),
    confidencePct: Math.round(safeNumber(item.confidence) * 100),
    valid: Boolean(item.valid),
    ts: fileUpdatedAt,
    bbox: {
      x: safeNumber(item.bbox?.x),
      y: safeNumber(item.bbox?.y),
      w: safeNumber(item.bbox?.w),
      h: safeNumber(item.bbox?.h),
    },
    frame: {
      width: safeNumber(frame.width),
      height: safeNumber(frame.height),
    },
    tracking: {
      enabled: Boolean(tracking.enabled),
      targetSelected: Boolean(tracking.target_selected),
      confidencePct: Math.round(safeNumber(tracking.confidence) * 100),
      offsetX: safeNumber(tracking.target_offset_x),
      offsetY: safeNumber(tracking.target_offset_y),
    },
  }));
}

function projectSectorPoint(origin, yawDeg, relativeDeg, distanceM) {
  const angleRad = ((yawDeg + relativeDeg) * Math.PI) / 180;
  return {
    x: origin.x + Math.cos(angleRad) * distanceM,
    y: origin.y + Math.sin(angleRad) * distanceM,
  };
}

export function buildMapPayload({ snapshot, history, fileUpdatedAt }) {
  const originPose = getPose(snapshot);
  const odomPose = snapshot.odom?.pose ?? {};

  const sectorAngles = {
    front: 0,
    front_left: 45,
    left: 90,
    rear_left: 135,
    rear: 180,
    rear_right: 225,
    right: 270,
    front_right: 315,
  };

  const sectorDistances = snapshot.lidar?.pose?.sectors_m ?? {};
  const rays = Object.entries(sectorDistances)
    .map(([sector, distanceM]) => ({
      sector,
      distanceM: safeNumber(distanceM, -1),
      angleDeg: sectorAngles[sector] ?? 0,
    }))
    .filter((item) => item.distanceM > 0)
    .map((item) => ({
      ...item,
      ...projectSectorPoint(
        { x: originPose.x, y: originPose.y },
        originPose.yawDeg,
        item.angleDeg,
        item.distanceM,
      ),
    }));

  const currentCloud = (snapshot.lidar?.snapshot?.point_cloud ?? []).map((point) => ({
    ...toWorldPoint(point, originPose),
    localX: safeNumber(point.x_m),
    localY: safeNumber(point.y_m),
  }));

  const historyClouds = history.flatMap((entry, historyIndex) => {
    const pose = getPose(entry);
    const age = history.length - historyIndex - 1;
    return (entry.lidar?.snapshot?.point_cloud ?? []).map((point) => ({
      ...toWorldPoint(point, pose),
      age,
    }));
  });

  const trail = history.map((entry) => {
    const pose = getPose(entry);
    return {
      x: pose.x,
      y: pose.y,
      yaw_deg: pose.yawDeg,
    };
  });

  const goal = snapshot.navigation?.goal?.active
    ? {
        x: safeNumber(snapshot.navigation.goal.x_m),
        y: safeNumber(snapshot.navigation.goal.y_m),
        distanceM: safeNumber(snapshot.navigation.goal.distance_m),
        bearingDeg: safeNumber(snapshot.navigation.goal.bearing_deg),
        headingErrorDeg: safeNumber(snapshot.navigation.goal.heading_error_deg),
        reached: Boolean(snapshot.navigation.goal.reached),
      }
    : null;

  const points = [
    { x: originPose.x, y: originPose.y },
    { x: safeNumber(odomPose.x_m), y: safeNumber(odomPose.y_m) },
    ...currentCloud.map((point) => ({ x: point.x, y: point.y })),
    ...historyClouds.map((point) => ({ x: point.x, y: point.y })),
    ...rays.map((ray) => ({ x: ray.x, y: ray.y })),
    ...trail.map((point) => ({ x: point.x, y: point.y })),
  ];

  if (goal) points.push({ x: goal.x, y: goal.y });

  const xs = points.map((point) => point.x);
  const ys = points.map((point) => point.y);
  const margin = 0.35;

  let minX = Math.min(...xs) - margin;
  let maxX = Math.max(...xs) + margin;
  let minY = Math.min(...ys) - margin;
  let maxY = Math.max(...ys) + margin;

  if (maxX - minX < 2) {
    const midX = (maxX + minX) / 2;
    minX = midX - 1;
    maxX = midX + 1;
  }

  if (maxY - minY < 2) {
    const midY = (maxY + minY) / 2;
    minY = midY - 1;
    maxY = midY + 1;
  }

  return {
    meta: {
      map_id: `lidar-debug-${safeNumber(snapshot.timestamp_ms)}`,
      timestamp: fileUpdatedAt,
      source: "jsonl-realtime-loop",
      mode: snapshot.robot?.mode ?? "Unknown",
      coordinateFrame:
        snapshot.lidar?.snapshot?.coordinate_frame ?? "robot_base_lidar_2d",
    },
    bounds: { minX, maxX, minY, maxY },
    robot: {
      pose: {
        x: originPose.x,
        y: originPose.y,
        yaw_deg: originPose.yawDeg,
      },
    },
    odom: {
      pose: {
        x: safeNumber(odomPose.x_m),
        y: safeNumber(odomPose.y_m),
        yaw_deg: safeNumber(odomPose.yaw_deg),
      },
    },
    goal,
    scan: {
      valid: Boolean(snapshot.lidar?.pose?.valid),
      confidence: safeNumber(snapshot.lidar?.pose?.confidence),
      pointCount: safeNumber(snapshot.lidar?.snapshot?.point_count),
      rays,
      nearestM: safeNumber(snapshot.lidar?.pose?.nearest_m, -1),
      sectors: sectorDistances,
      currentCloud,
      historyClouds,
      trail,
    },
    hints: snapshot.lidar?.hints ?? {},
    summary: snapshot.lidar?.summary ?? {},
    navigation: snapshot.navigation ?? {},
  };
}

export async function buildEnvAnalysisPayload() {
  const stat = await statRequired(LOG_PATH, "debug log file");

  const temps = [];
  const humidities = [];
  const pressures = [];
  const gases = [];
  const series = [];

  let totalEntries = 0;
  let envValid = 0;
  let imuValid = 0;
  let lidarValid = 0;
  let detectionsValid = 0;
  let firstTimestamp = null;

  await streamJsonLines(LOG_PATH, async (entry, index) => {
    totalEntries += 1;
    if (entry.m5stick?.env?.valid) {
      envValid += 1;
      const tempC = safeNumber(entry.m5stick.env.temp_c);
      const humidityPct = safeNumber(entry.m5stick.env.humidity_pct);
      const pressureHpa = safeNumber(entry.m5stick.env.pressure_hpa);
      const gasKohm = safeNumber(entry.m5stick.env.gas_kohm);

      temps.push(tempC);
      humidities.push(humidityPct);
      pressures.push(pressureHpa);
      gases.push(gasKohm);

      if (firstTimestamp == null) {
        firstTimestamp = safeNumber(entry.timestamp_ms);
      }

      const keepEvery = Math.max(1, Math.floor((index + 1) / ANALYSIS_SERIES_MAX));
      if (series.length < ANALYSIS_SERIES_MAX || index % keepEvery === 0) {
        if (series.length >= ANALYSIS_SERIES_MAX) series.shift();
        series.push({
          tSec: (safeNumber(entry.timestamp_ms) - firstTimestamp) / 1000,
          tempC,
          humidityPct,
          pressureHpa,
          gasKohm,
        });
      }
    }

    if (entry.m5stick?.imu?.valid) imuValid += 1;
    if (entry.lidar?.pose?.valid) lidarValid += 1;
    if (entry.perception?.detections_valid) detectionsValid += 1;
  });

  const correlations = {
    tempHumidity: correlation(temps, humidities),
    tempPressure: correlation(temps, pressures),
    tempGas: correlation(temps, gases),
    humidityPressure: correlation(humidities, pressures),
    humidityGas: correlation(humidities, gases),
    pressureGas: correlation(pressures, gases),
  };

  const rankedCorrelations = Object.entries(correlations)
    .filter(([, value]) => value != null)
    .sort((a, b) => Math.abs(b[1]) - Math.abs(a[1]));

  const findings = rankedCorrelations.slice(0, 3).map(([pair, value]) => ({
    pair,
    correlation: value,
    strength:
      Math.abs(value) > 0.9
        ? "very strong"
        : Math.abs(value) > 0.7
          ? "strong"
          : Math.abs(value) > 0.4
            ? "moderate"
            : "weak",
  }));

  return {
    ts: stat.mtime.toISOString(),
    sampleCount: temps.length,
    totalEntries,
    summary: {
      temperature: summarizeMetric(temps),
      humidity: summarizeMetric(humidities),
      pressure: summarizeMetric(pressures),
      gas: summarizeMetric(gases),
    },
    validity: {
      envPct: totalEntries ? envValid / totalEntries : 0,
      imuPct: totalEntries ? imuValid / totalEntries : 0,
      lidarPct: totalEntries ? lidarValid / totalEntries : 0,
      detectionsPct: totalEntries ? detectionsValid / totalEntries : 0,
    },
    correlations,
    findings,
    series,
  };
}
