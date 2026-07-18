import path from "node:path";
import { promises as fs } from "node:fs";

const SESSION_DATA_DIR = path.join(process.cwd(), "src", "session-data");
const SESSION_FOLDER_PATTERN = /^session[-_]\d{8}[-_]\d{6}$/;
const ALLOWED_MEDIA_DIRECTORIES = new Set([
  "images_ok",
  "images_ok_raw",
  "images_weak_noise",
  "images_weak_noise_raw",
  "videos",
]);

function toFiniteNumber(value) {
  if (typeof value === "number" && Number.isFinite(value)) return value;

  if (typeof value === "string" && value.trim() !== "") {
    const parsed = Number(value);
    return Number.isFinite(parsed) ? parsed : null;
  }

  return null;
}

function toBooleanOrNull(value) {
  return typeof value === "boolean" ? value : null;
}

function toStringOrNull(value) {
  return typeof value === "string" ? value : null;
}

function buildReadError(label, error) {
  const message = error instanceof Error ? error.message : "Unknown error";
  return new Error(`Could not read ${label}: ${message}`);
}

function assertValidSessionId(sessionId) {
  if (typeof sessionId !== "string" || !SESSION_FOLDER_PATTERN.test(sessionId)) {
    throw new Error(`Invalid robot session id: ${String(sessionId)}`);
  }
}

function normalizeRelativeMediaPath(relativePath) {
  if (typeof relativePath !== "string" || relativePath.trim() === "") {
    throw new Error("A robot-session media path is required.");
  }

  const normalized = path.posix.normalize(relativePath.replaceAll("\\", "/"));

  if (
    normalized === "." ||
    normalized.startsWith("/") ||
    normalized.startsWith("../") ||
    normalized.includes("/../")
  ) {
    throw new Error("Invalid robot-session media path.");
  }

  const firstSegment = normalized.split("/")[0];

  if (!ALLOWED_MEDIA_DIRECTORIES.has(firstSegment)) {
    throw new Error("Requested media directory is not allowed.");
  }

  return normalized;
}

async function readJsonFile(filePath, label) {
  try {
    const content = await fs.readFile(filePath, "utf8");
    return JSON.parse(content);
  } catch (error) {
    throw buildReadError(label, error);
  }
}

async function readOptionalJsonFile(filePath, label) {
  try {
    return await readJsonFile(filePath, label);
  } catch (error) {
    if (error?.cause?.code === "ENOENT" || /ENOENT/.test(error?.message ?? "")) {
      return null;
    }

    throw error;
  }
}

async function readJsonlFile(filePath, label) {
  let content;

  try {
    content = await fs.readFile(filePath, "utf8");
  } catch (error) {
    throw buildReadError(label, error);
  }

  const rows = [];
  const lines = content.split(/\r?\n/).filter((line) => line.trim() !== "");

  lines.forEach((line, index) => {
    try {
      rows.push(JSON.parse(line));
    } catch {
      throw new Error(`Invalid JSONL row ${index + 1} in ${label}.`);
    }
  });

  return rows;
}

async function readOptionalJsonlFile(filePath, label) {
  try {
    return await readJsonlFile(filePath, label);
  } catch (error) {
    if (error?.cause?.code === "ENOENT" || /ENOENT/.test(error?.message ?? "")) {
      return [];
    }

    throw error;
  }
}

function buildSessionSummary(folderName, manifest) {
  return {
    id: folderName,
    sourceSessionId: manifest?.session_id ?? null,
    startedAtLocal: manifest?.started_at_local ?? null,
    stoppedAtLocal: manifest?.stopped_at_local ?? null,
    stopReason: manifest?.stop_reason ?? null,
    schemaVersion: manifest?.schema_version ?? null,
    counts: manifest?.counts ?? {},
    paths: manifest?.paths ?? {},
  };
}

function normalizeCameraView(cameraView) {
  const direction = cameraView?.direction_robot ?? {};

  return {
    valid: toBooleanOrNull(cameraView?.valid),
    panRelativeDeg: toFiniteNumber(cameraView?.pan_relative_deg),
    tiltRelativeDeg: toFiniteNumber(cameraView?.tilt_relative_deg),
    digitalZoom: toFiniteNumber(cameraView?.digital_zoom),
    timestampMs: toFiniteNumber(cameraView?.timestamp_ms),
    directionRobot: {
      xForward: toFiniteNumber(direction?.x_forward),
      yLeft: toFiniteNumber(direction?.y_left),
      zUp: toFiniteNumber(direction?.z_up),
    },
  };
}


function normalizePolicyStatus(detection, weak) {
  const explicit = toStringOrNull(detection?.policy_status) ?? toStringOrNull(detection?.policyStatus);
  const normalized = explicit?.trim().toLowerCase();
  if (["strong", "review", "color_corrected", "heuristic", "weak", "noise"].includes(normalized)) return normalized;
  if (detection?.heuristic === true || detection?.source_type === "heuristic") return "heuristic";
  if (detection?.color_corrected_by_policy === true) return "color_corrected";
  if (detection?.review_candidate === true) return "review";
  const reason = String(detection?.reject_reason ?? "").toLowerCase();
  if (weak && reason.includes("noise")) return "noise";
  return weak ? "weak" : "strong";
}

function normalizeHeuristicMeta(meta) {
  if (!meta || typeof meta !== "object") return null;
  return {
    type: toStringOrNull(meta?.type),
    anchorBunchClass: toFiniteNumber(meta?.anchor_bunch_class),
    anchorBunchConfidence: toFiniteNumber(meta?.anchor_bunch_confidence),
    childCount: toFiniteNumber(meta?.child_count),
    weakChildCount: toFiniteNumber(meta?.weak_child_count),
    strongChildCount: toFiniteNumber(meta?.strong_child_count),
    weightedChildCount: toFiniteNumber(meta?.weighted_child_count),
    ripeEvidenceScore: toFiniteNumber(meta?.ripe_evidence_score),
    unripeEvidenceScore: toFiniteNumber(meta?.unripe_evidence_score),
    dominantMaturity: toStringOrNull(meta?.dominant_maturity),
    score: toFiniteNumber(meta?.score),
  };
}

function normalizeDetection(detection, eventId, detectionIndex) {
  const bbox = detection?.bbox ?? {};
  const metrics = detection?.metrics ?? {};
  const weak = toBooleanOrNull(detection?.weak) ?? false;
  const policyStatus = normalizePolicyStatus(detection, weak);

  return {
    id: `${eventId}-d${detectionIndex}`,
    label: toStringOrNull(detection?.label),
    classId: toFiniteNumber(detection?.class_id),
    confidence: toFiniteNumber(detection?.confidence),
    currentConfidence: toFiniteNumber(detection?.current_confidence),
    bestConfidence: toFiniteNumber(detection?.best_confidence),
    valid: toBooleanOrNull(detection?.valid),
    weak,
    policyStatus,
    sourceType: toStringOrNull(detection?.source_type) ?? "model",
    heuristic: policyStatus === "heuristic" || detection?.heuristic === true,
    reviewCandidate: policyStatus === "review" || detection?.review_candidate === true,
    reviewReason: toStringOrNull(detection?.review_reason),
    colorCorrectedByPolicy: policyStatus === "color_corrected" || detection?.color_corrected_by_policy === true,
    correctionReason: toStringOrNull(detection?.correction_reason),
    colorCorrectionScore: toFiniteNumber(detection?.color_correction_score),
    heuristicMeta: normalizeHeuristicMeta(detection?.heuristic_meta),
    displaySuppressed: toBooleanOrNull(detection?.display_suppressed),
    rejectReason: toStringOrNull(detection?.reject_reason),
    maturityScore: toFiniteNumber(detection?.maturity_score),
    maturityScoreRipe: toFiniteNumber(detection?.maturity_score_ripe),
    maturityScoreUnripe: toFiniteNumber(detection?.maturity_score_unripe),
    trackId: toFiniteNumber(detection?.track_id),
    trackHits: toFiniteNumber(detection?.track_hits),
    trackStableBest: toBooleanOrNull(detection?.track_stable_best),
    bbox: {
      x: toFiniteNumber(bbox?.x),
      y: toFiniteNumber(bbox?.y),
      w: toFiniteNumber(bbox?.w),
      h: toFiniteNumber(bbox?.h),
    },
    metrics: {
      boxArea: toFiniteNumber(metrics?.box_area),
      maskArea: toFiniteNumber(metrics?.mask_area),
      maskDensity: toFiniteNumber(metrics?.mask_density),
      redRatio: toFiniteNumber(metrics?.red_ratio),
      orangeRatio: toFiniteNumber(metrics?.orange_ratio),
      warmRatio: toFiniteNumber(metrics?.warm_ratio),
      greenYellowRatio: toFiniteNumber(metrics?.green_yellow_ratio),
    },
  };
}

function normalizeDetectionEvent(event, index) {
  const eventId = [
    toFiniteNumber(event?.timestamp_ms) ?? "unknown-time",
    toStringOrNull(event?.event_type) ?? "unknown-event",
    index,
  ].join("-");

  const detections = Array.isArray(event?.detections)
    ? event.detections.map((detection, detectionIndex) =>
        normalizeDetection(detection, eventId, detectionIndex),
      )
    : [];

  const acceptedDetections = detections.filter(
    (detection) =>
      detection.valid === true &&
      ["strong", "review", "color_corrected", "heuristic"].includes(detection.policyStatus) &&
      detection.displaySuppressed !== true,
  );

  return {
    id: eventId,
    eventType: toStringOrNull(event?.event_type),
    timestampMs: toFiniteNumber(event?.timestamp_ms),
    timestampLocal: toStringOrNull(event?.timestamp_local),
    imagePath: toStringOrNull(event?.image_path),
    annotatedImagePath: toStringOrNull(event?.annotated_image_path),
    rawImagePath: toStringOrNull(event?.raw_image_path),
    frame: {
      width: toFiniteNumber(event?.frame?.width),
      height: toFiniteNumber(event?.frame?.height),
      channels: toFiniteNumber(event?.frame?.channels),
      timestampMs: toFiniteNumber(event?.frame?.timestamp_ms),
    },
    camera: normalizeCameraView(event?.camera_view),
    acceptedCount: toFiniteNumber(event?.accepted_count),
    weakCount: toFiniteNumber(event?.weak_count),
    rejectedCount: toFiniteNumber(event?.rejected_count),
    detections,
    acceptedDetections,
  };
}

function normalizeLidarPreview(payload) {
  const rawPoints = Array.isArray(payload?.points) ? payload.points : [];

  const points = rawPoints
    .map((point) => ({
      xM: toFiniteNumber(point?.x_m),
      yM: toFiniteNumber(point?.y_m),
      distanceM: toFiniteNumber(point?.distance_m),
      angleDeg: toFiniteNumber(point?.angle_deg),
      timestampMs: toFiniteNumber(point?.timestamp_ms),
    }))
    .filter(
      (point) =>
        point.xM != null &&
        point.yM != null &&
        point.distanceM != null &&
        point.timestampMs != null,
    );

  return {
    schemaVersion: toFiniteNumber(payload?.schema_version),
    kind: toStringOrNull(payload?.kind),
    coordinateFrame: toStringOrNull(payload?.coordinate_frame),
    units: payload?.units ?? {},
    gridResolutionM: toFiniteNumber(payload?.grid_resolution_m),
    scansAcceptedForPreview: toFiniteNumber(payload?.scans_accepted_for_preview),
    scansSkippedNoMotion: toFiniteNumber(payload?.scans_skipped_no_motion),
    reportedPointCount: toFiniteNumber(payload?.point_count),
    pointCount: points.length,
    points,
  };
}

function normalizeTimelineRow(row) {
  return {
    timestampMs: toFiniteNumber(row?.timestamp_ms),
    timestampLocal: toStringOrNull(row?.timestamp_local),
    temperatureC: toFiniteNumber(row?.environment?.temp_c),
    humidityPct: toFiniteNumber(row?.environment?.humidity_pct),
    pressureHpa: toFiniteNumber(row?.environment?.pressure_hpa),
    environmentValid: toBooleanOrNull(row?.environment?.valid),
    environmentFresh: toBooleanOrNull(row?.environment?.fresh),
    acceptedCount: toFiniteNumber(row?.perception?.accepted_count),
    weakCount: toFiniteNumber(row?.perception?.weak_count),
    rejectedCount: toFiniteNumber(row?.perception?.rejected_count),
    reviewCount: toFiniteNumber(row?.perception?.review_count),
    colorCorrectedCount: toFiniteNumber(row?.perception?.color_corrected_count),
    heuristicCount: toFiniteNumber(row?.perception?.heuristic_count),
    noiseCount: toFiniteNumber(row?.perception?.noise_count),
    frontDistanceM: toFiniteNumber(row?.lidar?.front_m),
    leftDistanceM: toFiniteNumber(row?.lidar?.left_m),
    rightDistanceM: toFiniteNumber(row?.lidar?.right_m),
    rearDistanceM: toFiniteNumber(row?.lidar?.rear_m),
    obstacleClose: toBooleanOrNull(row?.lidar?.any_close),
    lidarValid: toBooleanOrNull(row?.lidar?.valid),
    lidarFresh: toBooleanOrNull(row?.lidar?.fresh),
    headingHintDeg: toFiniteNumber(row?.lidar?.heading_hint_deg),
    centerErrorM: toFiniteNumber(row?.lidar?.center_error_m),
    panRelativeDeg: toFiniteNumber(row?.camera_view?.pan_relative_deg),
    tiltRelativeDeg: toFiniteNumber(row?.camera_view?.tilt_relative_deg),
    digitalZoom: toFiniteNumber(row?.camera_view?.digital_zoom),
    cameraValid: toBooleanOrNull(row?.camera_view?.valid),
    forwardSpeed: toFiniteNumber(row?.drive?.forward_speed),
    steeringSpeed: toFiniteNumber(row?.drive?.steering_speed),
    warningActive: toBooleanOrNull(row?.robot?.warning_active),
    emergencyStop: toBooleanOrNull(row?.robot?.emergency_stop),
    robotStatus: toStringOrNull(row?.robot?.status_text),
  };
}

export function getRobotSessionDirectory(sessionId) {
  assertValidSessionId(sessionId);
  return path.join(SESSION_DATA_DIR, sessionId);
}

export async function resolveRobotSessionMediaFile(sessionId, requestedPath) {
  const sessionDirectory = getRobotSessionDirectory(sessionId);
  const safeRelativePath = normalizeRelativeMediaPath(requestedPath);
  const filePath = path.join(sessionDirectory, safeRelativePath);
  const relativeToSession = path.relative(sessionDirectory, filePath);

  if (
    relativeToSession.startsWith("..") ||
    path.isAbsolute(relativeToSession)
  ) {
    throw new Error("Invalid robot-session media path.");
  }

  let stat;

  try {
    stat = await fs.stat(filePath);
  } catch (error) {
    throw buildReadError(`media file ${safeRelativePath}`, error);
  }

  if (!stat.isFile()) {
    throw new Error("Requested robot-session media entry is not a file.");
  }

  return {
    filePath,
    relativePath: safeRelativePath,
    fileName: path.basename(filePath),
    sizeBytes: stat.size,
  };
}

export async function listRobotSessions() {
  let entries;

  try {
    entries = await fs.readdir(SESSION_DATA_DIR, { withFileTypes: true });
  } catch (error) {
    if (error?.code === "ENOENT") return [];
    throw error;
  }

  const sessions = await Promise.all(
    entries
      .filter(
        (entry) =>
          entry.isDirectory() && SESSION_FOLDER_PATTERN.test(entry.name),
      )
      .map(async (entry) => {
        const manifestPath = path.join(
          SESSION_DATA_DIR,
          entry.name,
          "session_manifest.json",
        );

        try {
          const manifest = await readJsonFile(
            manifestPath,
            `${entry.name}/session_manifest.json`,
          );
          return buildSessionSummary(entry.name, manifest);
        } catch {
          return null;
        }
      }),
  );

  return sessions
    .filter(Boolean)
    .sort((a, b) =>
      String(b.startedAtLocal ?? b.id).localeCompare(
        String(a.startedAtLocal ?? a.id),
      ),
    );
}

export async function readRobotSession(sessionId) {
  const sessionDirectory = getRobotSessionDirectory(sessionId);

  const [
    manifest,
    latest,
    timelineRows,
    detectionEventRows,
    lidarPreviewDocument,
  ] = await Promise.all([
    readJsonFile(
      path.join(sessionDirectory, "session_manifest.json"),
      `${sessionId}/session_manifest.json`,
    ),
    readJsonFile(
      path.join(sessionDirectory, "latest.json"),
      `${sessionId}/latest.json`,
    ),
    readOptionalJsonlFile(
      path.join(sessionDirectory, "robot_timeline.jsonl"),
      `${sessionId}/robot_timeline.jsonl`,
    ),
    readOptionalJsonlFile(
      path.join(sessionDirectory, "detection_events.jsonl"),
      `${sessionId}/detection_events.jsonl`,
    ),
    readOptionalJsonFile(
      path.join(sessionDirectory, "lidar", "lidar_map_preview.json"),
      `${sessionId}/lidar/lidar_map_preview.json`,
    ),
  ]);

  return {
    session: buildSessionSummary(sessionId, manifest),
    latest,
    timeline: timelineRows.map(normalizeTimelineRow),
    detectionEvents: detectionEventRows.map(normalizeDetectionEvent),
    lidarPreview: normalizeLidarPreview(lidarPreviewDocument ?? {}),
  };
}
