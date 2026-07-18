import path from "node:path";
import { existsSync } from "node:fs";
import { promises as fs } from "node:fs";

const DEFAULT_SESSION_ROOT = path.join(process.cwd(), "src", "session-data");
const DEFAULT_PUBLIC_MEDIA_ROOT = path.join(process.cwd(), "public", "session-media");
const MAX_DETECTION_MARKERS = 5000;
const MAX_TRAIL_POINTS = 10000;

function safeNumber(value, fallback = 0) {
  const num = Number(value);
  return Number.isFinite(num) ? num : fallback;
}

function optionalNumber(value, fallback = null) {
  if (value === null || value === undefined || value === "") return fallback;
  const num = Number(value);
  return Number.isFinite(num) ? num : fallback;
}

function safeString(value, fallback = "") {
  return typeof value === "string" ? value : fallback;
}

function normalizeRelPath(value) {
  return safeString(value)
    .replaceAll("\\", "/")
    .replaceAll("//", "/")
    .replace(/^\.\//, "")
    .replace(/^\/+/, "");
}

function resolveSessionRoot() {
  return process.env.ROBOT_DASHBOARD_SESSION_ROOT || DEFAULT_SESSION_ROOT;
}

function resolveConfiguredSessionDir() {
  return process.env.ROBOT_DASHBOARD_SESSION_DIR || null;
}

function sanitizeSessionId(sessionId) {
  const cleaned = safeString(sessionId).trim();
  if (!cleaned) return "";
  if (!/^session_[A-Za-z0-9_-]+$/.test(cleaned)) return "";
  return cleaned;
}

export function resolveDashboardSessionFilePath(sessionId, relativePath) {
  const cleanSessionId = sanitizeSessionId(sessionId);
  if (!cleanSessionId) throw new Error("Invalid session id.");

  const cleanRelative = normalizeRelPath(relativePath);
  if (!cleanRelative || cleanRelative.includes("..")) {
    throw new Error("Invalid session file path.");
  }

  const sessionDir = path.join(resolveSessionRoot(), cleanSessionId);
  const filePath = path.resolve(sessionDir, cleanRelative);
  const sessionRoot = path.resolve(sessionDir);

  if (!filePath.startsWith(sessionRoot + path.sep) && filePath !== sessionRoot) {
    throw new Error("Session file path escapes the selected session.");
  }

  return filePath;
}

function sessionAssetUrl(sessionId, relativePath) {
  const rel = normalizeRelPath(relativePath);
  if (!sessionId || !rel) return null;
  return `/api/session-file?session=${encodeURIComponent(sessionId)}&path=${encodeURIComponent(rel)}`;
}

function browserVideoFileName(relativePath) {
  const normalized = normalizeRelPath(relativePath);
  if (!normalized) return "";
  const ext = path.extname(normalized).toLowerCase();
  const base = path.basename(normalized, ext);
  const lowerBase = base.toLowerCase();

  if (ext === ".webm" || lowerBase.includes("browser") || lowerBase.includes("h264")) {
    return path.basename(normalized);
  }

  return `${base}_browser.mp4`;
}

function publicSessionVideoRelPath(sessionId, relativePath) {
  const fileName = browserVideoFileName(relativePath);
  if (!sessionId || !fileName) return "";
  return normalizeRelPath(path.join(sessionId, "videos", fileName));
}

function publicSessionVideoUrl(sessionId, relativePath) {
  const rel = publicSessionVideoRelPath(sessionId, relativePath);
  if (!rel) return null;
  return `/session-media/${rel}`;
}

async function publicSessionVideoExists(sessionId, relativePath) {
  const rel = publicSessionVideoRelPath(sessionId, relativePath);
  if (!rel) return false;
  return fileExists(path.join(DEFAULT_PUBLIC_MEDIA_ROOT, rel));
}

async function pathIsDir(dirPath) {
  try {
    const stat = await fs.stat(dirPath);
    return stat.isDirectory();
  } catch {
    return false;
  }
}

async function fileExists(filePath) {
  try {
    const stat = await fs.stat(filePath);
    return stat.isFile();
  } catch {
    return false;
  }
}

async function readJson(filePath) {
  return JSON.parse(await fs.readFile(filePath, "utf8"));
}

async function readJsonLines(filePath) {
  const exists = await fileExists(filePath);
  if (!exists) return [];

  const text = await fs.readFile(filePath, "utf8");
  const rows = [];

  for (const line of text.split(/\r?\n/)) {
    const trimmed = line.trim();
    if (!trimmed) continue;
    if (trimmed.startsWith("version https://git-lfs.github.com/spec/")) continue;
    try {
      rows.push(JSON.parse(trimmed));
    } catch {
      // Keep the dashboard usable if one exported row is incomplete/corrupted.
    }
  }

  return rows;
}

function parseYamlScalar(value) {
  const trimmed = value.trim();
  if (!trimmed) return "";
  if (
    (trimmed.startsWith('"') && trimmed.endsWith('"')) ||
    (trimmed.startsWith("'") && trimmed.endsWith("'"))
  ) {
    return trimmed.slice(1, -1);
  }
  return trimmed;
}

function parseRosMapYaml(text) {
  const data = {};

  for (const rawLine of text.split(/\r?\n/)) {
    const line = rawLine.replace(/#.*$/, "").trim();
    if (!line) continue;

    const colonIndex = line.indexOf(":");
    if (colonIndex < 0) continue;

    const key = line.slice(0, colonIndex).trim();
    const value = line.slice(colonIndex + 1).trim();
    data[key] = parseYamlScalar(value);
  }

  const originValues = String(data.origin ?? "")
    .replace(/[\[\]]/g, "")
    .split(",")
    .map((item) => Number(item.trim()))
    .filter((item) => Number.isFinite(item));

  return {
    image: safeString(data.image, "map.pgm"),
    mode: safeString(data.mode, "trinary"),
    resolution: safeNumber(data.resolution, 0.05),
    origin: {
      x: safeNumber(originValues[0]),
      y: safeNumber(originValues[1]),
      yawRad: safeNumber(originValues[2]),
    },
    negate: safeNumber(data.negate),
    occupiedThresh: safeNumber(data.occupied_thresh, 0.65),
    freeThresh: safeNumber(data.free_thresh, 0.25),
  };
}

function skipWhitespaceAndComments(buffer, offset) {
  let current = offset;

  while (current < buffer.length) {
    const byte = buffer[current];

    if (byte === 35) {
      while (current < buffer.length && buffer[current] !== 10) current += 1;
      continue;
    }

    if (byte === 9 || byte === 10 || byte === 13 || byte === 32) {
      current += 1;
      continue;
    }

    break;
  }

  return current;
}

function nextPgmToken(buffer, offset) {
  let current = skipWhitespaceAndComments(buffer, offset);
  const start = current;

  while (current < buffer.length) {
    const byte = buffer[current];
    if (byte === 9 || byte === 10 || byte === 13 || byte === 32 || byte === 35) {
      break;
    }
    current += 1;
  }

  return {
    token: buffer.toString("ascii", start, current),
    nextOffset: current,
  };
}

function parsePgm(buffer) {
  let offset = 0;
  const magicToken = nextPgmToken(buffer, offset);
  const magic = magicToken.token;
  offset = magicToken.nextOffset;

  if (magic !== "P5" && magic !== "P2") {
    throw new Error(`Unsupported PGM format ${magic || "<empty>"}. Expected P5 or P2.`);
  }

  const widthToken = nextPgmToken(buffer, offset);
  offset = widthToken.nextOffset;
  const heightToken = nextPgmToken(buffer, offset);
  offset = heightToken.nextOffset;
  const maxToken = nextPgmToken(buffer, offset);
  offset = maxToken.nextOffset;

  const width = Number(widthToken.token);
  const height = Number(heightToken.token);
  const maxValue = Number(maxToken.token);

  if (!Number.isFinite(width) || !Number.isFinite(height) || width <= 0 || height <= 0) {
    throw new Error("Invalid PGM width/height.");
  }

  if (!Number.isFinite(maxValue) || maxValue <= 0) {
    throw new Error("Invalid PGM max value.");
  }

  const pixelCount = width * height;

  if (magic === "P5") {
    const pixelStart = skipWhitespaceAndComments(buffer, offset);
    const bytesPerPixel = maxValue > 255 ? 2 : 1;
    const expectedLength = pixelCount * bytesPerPixel;
    const source = buffer.subarray(pixelStart, pixelStart + expectedLength);

    if (source.length < expectedLength) {
      throw new Error("PGM file is shorter than expected.");
    }

    const pixels = Buffer.alloc(pixelCount);
    if (bytesPerPixel === 1) {
      source.copy(pixels, 0, 0, pixelCount);
    } else {
      for (let i = 0; i < pixelCount; i += 1) {
        const raw = source.readUInt16BE(i * 2);
        pixels[i] = Math.round((raw / maxValue) * 255);
      }
    }

    return { magic, width, height, maxValue, pixels };
  }

  const ascii = buffer.toString("ascii", offset);
  const values = ascii
    .replace(/#[^\n\r]*/g, " ")
    .trim()
    .split(/\s+/)
    .map((item) => Number(item))
    .filter((item) => Number.isFinite(item));

  if (values.length < pixelCount) {
    throw new Error("ASCII PGM file is shorter than expected.");
  }

  const pixels = Buffer.alloc(pixelCount);
  for (let i = 0; i < pixelCount; i += 1) {
    pixels[i] = Math.round((values[i] / maxValue) * 255);
  }

  return { magic, width, height, maxValue, pixels };
}

function sessionDateFromId(sessionId) {
  const match = /^session_(\d{4})(\d{2})(\d{2})_(\d{2})(\d{2})(\d{2})/.exec(sessionId);
  if (!match) return null;
  const [, year, month, day, hour, minute, second] = match;
  return `${year}-${month}-${day} ${hour}:${minute}:${second}`;
}

async function buildSessionListItem(sessionDir, entryName) {
  const summaryPath = path.join(sessionDir, "map_overlay_summary.json");
  const manifestPath = path.join(sessionDir, "session_manifest.json");
  const posePath = path.join(sessionDir, "map_pose_timeline.jsonl");
  const detectionsPath = path.join(sessionDir, "detections_on_map.jsonl");

  const [sessionStat, summaryStat, summary, manifest, hasPoseTimeline, hasDetections] = await Promise.all([
    fs.stat(sessionDir).catch(() => null),
    fs.stat(summaryPath).catch(() => null),
    fileExists(summaryPath).then((ok) => (ok ? readJson(summaryPath).catch(() => null) : null)),
    fileExists(manifestPath).then((ok) => (ok ? readJson(manifestPath).catch(() => null) : null)),
    fileExists(posePath),
    fileExists(detectionsPath),
  ]);

  const label = manifest?.started_at_local || summary?.session_id || sessionDateFromId(entryName) || entryName;
  const startedAt = manifest?.started_at_local || sessionDateFromId(entryName);
  const stoppedAt = manifest?.stopped_at_local || null;

  return {
    id: entryName,
    label,
    startedAt,
    stoppedAt,
    updatedAtMs: summaryStat?.mtimeMs ?? sessionStat?.mtimeMs ?? 0,
    mapAvailable: Boolean(summary?.map?.valid ?? summary?.map?.loaded),
    hasPoseTimeline,
    hasDetections,
    counts: {
      poseRows: safeNumber(summary?.counts?.map_pose_rows ?? manifest?.counts?.map_pose_rows, 0),
      detectionEvents: safeNumber(
        summary?.counts?.detections_on_map_events ?? manifest?.counts?.detection_events,
        0,
      ),
      okImages: safeNumber(manifest?.counts?.ok_images, 0),
      weakImages: safeNumber(manifest?.counts?.weak_noise_images, 0),
    },
  };
}

export async function listDashboardSessions() {
  const root = resolveSessionRoot();
  if (!(await pathIsDir(root))) return [];

  const entries = await fs.readdir(root, { withFileTypes: true });
  const sessionDirs = entries
    .filter((entry) => entry.isDirectory() && entry.name.startsWith("session_"))
    .map((entry) => entry.name);

  const items = [];
  for (const entryName of sessionDirs) {
    const sessionDir = path.join(root, entryName);
    items.push(await buildSessionListItem(sessionDir, entryName));
  }

  items.sort((a, b) => b.id.localeCompare(a.id) || b.updatedAtMs - a.updatedAtMs);
  return items;
}

async function findSessionDir(sessionId = "") {
  const configured = resolveConfiguredSessionDir();
  if (configured && !sessionId) {
    if (await pathIsDir(configured)) return configured;
    throw new Error(`ROBOT_DASHBOARD_SESSION_DIR does not exist: ${configured}`);
  }

  const root = resolveSessionRoot();
  if (!(await pathIsDir(root))) return null;

  const cleanSessionId = sanitizeSessionId(sessionId);
  if (cleanSessionId) {
    const requested = path.join(root, cleanSessionId);
    if (!(await pathIsDir(requested))) {
      throw new Error(`Dashboard session does not exist: ${cleanSessionId}`);
    }
    return requested;
  }

  const sessions = await listDashboardSessions();
  return sessions[0]?.id ? path.join(root, sessions[0].id) : null;
}

function resolveMapAssets(sessionDir, summary, yaml) {
  const copiedYamlRel = normalizeRelPath(summary?.paths?.copied_map_yaml);
  const copiedPgmRel = normalizeRelPath(summary?.paths?.copied_map_pgm);

  let yamlPath = copiedYamlRel ? path.join(sessionDir, copiedYamlRel) : null;
  let pgmPath = copiedPgmRel ? path.join(sessionDir, copiedPgmRel) : null;

  if (!yamlPath || !existsSync(yamlPath)) {
    const sessionName = path.basename(safeString(summary?.paths?.copied_map_session_dir));
    if (sessionName) {
      yamlPath = path.join(sessionDir, "ros2_map", "map_session", sessionName, "map.yaml");
    }
  }

  if (!pgmPath || !existsSync(pgmPath)) {
    const imageName = path.basename(safeString(yaml?.image, "map.pgm"));
    if (yamlPath) {
      pgmPath = path.join(path.dirname(yamlPath), imageName);
    }
  }

  return { yamlPath, pgmPath };
}

function poseFromRow(row) {
  const pose = row?.map_pose ?? row ?? {};
  const x = safeNumber(pose.robot_x, NaN);
  const y = safeNumber(pose.robot_y, NaN);

  if (!Number.isFinite(x) || !Number.isFinite(y)) return null;

  return {
    timestampMs: safeNumber(row.timestamp_ms ?? row.timestampMs),
    timestampLocal: row.timestamp_local ?? row.timestampLocal ?? null,
    x,
    y,
    yawDeg: safeNumber(pose.robot_yaw_deg ?? pose.yawDeg),
    distanceM: safeNumber(pose.robot_distance_m ?? pose.distanceM),
    trailCount: safeNumber(pose.trail_count ?? pose.trailCount),
    manualStartSet: Boolean(pose.manual_start_set ?? pose.manualStartSet),
  };
}

function normalizeTomatoCategory(label, classId) {
  const raw = safeString(label).toLowerCase().replaceAll("_", " ").trim();
  const isHeuristic = raw.includes("heuristic");
  const isMixed = raw.includes("mixed");
  const isBunch =
    raw.includes("bunch") ||
    raw.includes("cluster") ||
    classId === 0 ||
    classId === 3 ||
    classId === 4 ||
    classId === 5;
  const isUnripe = raw.includes("unripe") || raw.includes("green") || raw.includes("dgreen");
  const isRipe =
    raw.includes("ripe") ||
    raw.includes("turning") ||
    raw.includes("eripe") ||
    raw.includes("bripe");

  if ((isHeuristic || isBunch) && isMixed) return "mixed_bunch";
  if (isBunch && isUnripe) return "unripe_bunch";
  if (isBunch && isRipe) return "ripe_bunch";
  if (isBunch) return "ripe_bunch";
  if (isUnripe) return "unripe_tomato";
  if (isRipe) return "ripe_tomato";
  return "unknown";
}

function categoryLabel(category) {
  switch (category) {
    case "ripe_tomato":
      return "Ripe tomato";
    case "unripe_tomato":
      return "Unripe tomato";
    case "ripe_bunch":
      return "Ripe bunch";
    case "unripe_bunch":
      return "Unripe bunch";
    case "mixed_bunch":
      return "Mixed bunch";
    default:
      return "Unknown";
  }
}

function normalizePolicyStatus(candidate, fallbackWeak = false) {
  const explicit = safeString(candidate?.policy_status || candidate?.policyStatus).toLowerCase().trim();
  if (["strong", "review", "color_corrected", "heuristic", "weak", "noise"].includes(explicit)) {
    return explicit;
  }
  if (candidate?.heuristic === true || safeString(candidate?.source_type).toLowerCase() === "heuristic") return "heuristic";
  if (candidate?.color_corrected_by_policy === true) return "color_corrected";
  if (candidate?.review_candidate === true) return "review";
  return fallbackWeak ? "weak" : "strong";
}

function policyStatusLabel(status) {
  switch (status) {
    case "strong":
      return "Strong";
    case "review":
      return "Review";
    case "color_corrected":
      return "Color corrected";
    case "heuristic":
      return "Heuristics";
    case "noise":
      return "Noise";
    case "weak":
      return "Weak";
    default:
      return "Unknown";
  }
}

function normalizeProjectionMethod(method) {
  const raw = safeString(method, "unknown");
  if (raw.includes("camera_bearing")) return "approx camera bearing";
  return raw.replaceAll("_", " ");
}



function evidenceKeyFromRow(row) {
  const timestampMs = safeNumber(row?.timestamp_ms ?? row?.timestampMs);
  const rawImagePath = normalizeRelPath(row?.raw_image_path || row?.rawImagePath || row?.image_path || row?.imagePath || row?.annotated_image_path);
  return `${timestampMs}|${rawImagePath}`;
}

function normalizeBbox(bbox) {
  const x = safeNumber(bbox?.x, NaN);
  const y = safeNumber(bbox?.y, NaN);
  const w = safeNumber(bbox?.w, NaN);
  const h = safeNumber(bbox?.h, NaN);
  const valid = Boolean(bbox?.valid ?? true) && Number.isFinite(x) && Number.isFinite(y) && Number.isFinite(w) && Number.isFinite(h) && w > 0 && h > 0;
  return { valid, x: valid ? x : 0, y: valid ? y : 0, w: valid ? w : 0, h: valid ? h : 0 };
}

function boxCenter(bbox) {
  const box = normalizeBbox(bbox);
  if (!box.valid) return null;
  return { x: box.x + box.w / 2, y: box.y + box.h / 2 };
}

function normalizeImageCenter(imageCenter, bbox, frameWidth = 1280, frameHeight = 720) {
  const directX = optionalNumber(imageCenter?.x_norm ?? imageCenter?.xNorm, null);
  const directY = optionalNumber(imageCenter?.y_norm ?? imageCenter?.yNorm, null);
  if (directX !== null && directY !== null) {
    return {
      xNorm: Math.min(1, Math.max(0, directX)),
      yNorm: Math.min(1, Math.max(0, directY)),
      source: "exported_image_center",
    };
  }

  const box = normalizeBbox(bbox);
  const width = optionalNumber(frameWidth, 1280) || 1280;
  const height = optionalNumber(frameHeight, 720) || 720;
  if (box.valid && width > 0 && height > 0) {
    return {
      xNorm: Math.min(1, Math.max(0, (box.x + box.w / 2) / width)),
      yNorm: Math.min(1, Math.max(0, (box.y + box.h / 2) / height)),
      source: "bbox_center_estimate",
    };
  }

  return { xNorm: 0.5, yNorm: 0.5, source: "fallback_center" };
}

function normalizeCameraView(cameraView, projection = {}) {
  const centerPanDeg = optionalNumber(cameraView?.center_pan_deg ?? cameraView?.centerPanDeg, 90);
  const panServoLowerDeg = optionalNumber(cameraView?.pan_servo_lower_deg ?? cameraView?.panServoLowerDeg, null);
  const panRelativeDeg = optionalNumber(
    cameraView?.pan_relative_deg ?? cameraView?.panRelativeDeg,
    optionalNumber(projection?.camera_pan_left_deg ?? projection?.cameraPanLeftDeg, null),
  );
  const side = panRelativeDeg == null ? "unknown" : panRelativeDeg < 0 ? "right" : panRelativeDeg > 0 ? "left" : "center";

  return {
    valid: Boolean(cameraView?.valid ?? panRelativeDeg != null),
    panServoLowerDeg,
    tiltServoUpperDeg: optionalNumber(cameraView?.tilt_servo_upper_deg ?? cameraView?.tiltServoUpperDeg, null),
    centerPanDeg,
    centerTiltDeg: optionalNumber(cameraView?.center_tilt_deg ?? cameraView?.centerTiltDeg, 90),
    panRelativeDeg,
    tiltRelativeDeg: optionalNumber(cameraView?.tilt_relative_deg ?? cameraView?.tiltRelativeDeg, null),
    side,
    digitalZoom: optionalNumber(cameraView?.digital_zoom ?? cameraView?.digitalZoom, 1),
    timestampMs: optionalNumber(cameraView?.timestamp_ms ?? cameraView?.timestampMs, null),
    directionRobot: cameraView?.direction_robot ?? cameraView?.directionRobot ?? null,
  };
}

function pointInsideBox(point, bbox) {
  const box = normalizeBbox(bbox);
  if (!box.valid || !point) return false;
  return point.x >= box.x && point.x <= box.x + box.w && point.y >= box.y && point.y <= box.y + box.h;
}

function boxIntersectionRatio(inner, outer) {
  const a = normalizeBbox(inner);
  const b = normalizeBbox(outer);
  if (!a.valid || !b.valid) return 0;
  const x1 = Math.max(a.x, b.x);
  const y1 = Math.max(a.y, b.y);
  const x2 = Math.min(a.x + a.w, b.x + b.w);
  const y2 = Math.min(a.y + a.h, b.y + b.h);
  const area = Math.max(0, x2 - x1) * Math.max(0, y2 - y1);
  return area / Math.max(1, a.w * a.h);
}

function normalizeCandidate(candidate, index, source, key) {
  const label = safeString(candidate?.label, "unknown");
  const classId = safeNumber(candidate?.class_id, -1);
  const category = normalizeTomatoCategory(label, classId);
  const confidence = safeNumber(candidate?.confidence, null);
  const weak = Boolean(candidate?.weak);
  const policyStatus = normalizePolicyStatus(candidate, weak);
  const sourceType = safeString(candidate?.source_type || candidate?.sourceType, candidate?.heuristic ? "heuristic" : "model");
  const isHeuristic = policyStatus === "heuristic" || sourceType === "heuristic" || Boolean(candidate?.heuristic);
  const isColorCorrected = policyStatus === "color_corrected" || Boolean(candidate?.color_corrected_by_policy);
  const isReview = policyStatus === "review" || Boolean(candidate?.review_candidate);
  const bbox = normalizeBbox(candidate?.bbox);
  const originalBbox = normalizeBbox(candidate?.original_bbox);
  const refinedBbox = normalizeBbox(candidate?.refined_bbox);

  return {
    id: `${key}:${source}:${index}`,
    source,
    sourceLabel: source === "raw" ? "Raw candidate" : "Final decision",
    index,
    label,
    classId,
    category,
    categoryLabel: categoryLabel(category),
    confidence,
    confidencePct: confidence == null ? null : Math.round(confidence * 100),
    currentConfidence: safeNumber(candidate?.current_confidence, null),
    bestConfidence: safeNumber(candidate?.best_confidence, null),
    valid: Boolean(candidate?.valid ?? true),
    weak,
    sourceType,
    policyStatus,
    policyStatusLabel: policyStatusLabel(policyStatus),
    heuristic: isHeuristic,
    reviewCandidate: isReview,
    reviewReason: safeString(candidate?.review_reason || candidate?.reviewReason),
    colorCorrectedByPolicy: isColorCorrected,
    correctionReason: safeString(candidate?.correction_reason || candidate?.correctionReason),
    colorCorrectionScore: safeNumber(candidate?.color_correction_score ?? candidate?.colorCorrectionScore, null),
    quality: policyStatus,
    accepted:
      source === "final" &&
      ["strong", "review", "color_corrected", "heuristic"].includes(policyStatus) &&
      Boolean(candidate?.valid ?? true) &&
      !candidate?.display_suppressed,
    displaySuppressed: Boolean(candidate?.display_suppressed),
    rejectReason: safeString(candidate?.reject_reason),
    bbox,
    originalBbox,
    refinedBbox,
    trackId: safeNumber(candidate?.track_id, -1),
    trackHits: safeNumber(candidate?.track_hits, 0),
    trackAge: safeNumber(candidate?.track_age, 0),
    trackStableBest: Boolean(candidate?.track_stable_best),
    clusterPromoted: Boolean(candidate?.cluster_promoted),
    classCorrected: Boolean(candidate?.class_corrected),
    originalClassId: safeNumber(candidate?.original_class_id, -1),
    originalLabel: safeString(candidate?.original_label),
    displaySource: safeString(candidate?.display_source),
    promotionReason: safeString(candidate?.promotion_reason),
    maturity: {
      competitionWinner: Boolean(candidate?.maturity_competition_winner),
      lostCompetition: Boolean(candidate?.lost_maturity_competition),
      classLocked: Boolean(candidate?.class_locked),
      switchCandidateFrames: safeNumber(candidate?.switch_candidate_frames, 0),
      score: safeNumber(candidate?.maturity_score, null),
      ripeScore: safeNumber(candidate?.maturity_score_ripe, null),
      unripeScore: safeNumber(candidate?.maturity_score_unripe, null),
      reason: safeString(candidate?.maturity_competition_reason),
    },
    roi: {
      pass: Boolean(candidate?.roi_pass),
      groupSize: safeNumber(candidate?.roi_group_size, 0),
      reason: safeString(candidate?.roi_reason),
      sourceAcceptedCount: safeNumber(candidate?.roi_source_accepted_count, 0),
      sourceWeakCount: safeNumber(candidate?.roi_source_weak_count, 0),
      paddingRatio: safeNumber(candidate?.roi_padding_ratio, 0),
    },
    support: {
      memberCount: safeNumber(candidate?.support?.member_count, 0),
      ripeCount: safeNumber(candidate?.support?.ripe_count, 0),
      unripeCount: safeNumber(candidate?.support?.unripe_count, 0),
      confSum: safeNumber(candidate?.support?.conf_sum, null),
      ripeConfSum: safeNumber(candidate?.support?.ripe_conf_sum, null),
      unripeConfSum: safeNumber(candidate?.support?.unripe_conf_sum, null),
      score: safeNumber(candidate?.support?.score, null),
    },
    heuristicMeta: {
      type: safeString(candidate?.heuristic_meta?.type || candidate?.heuristicMeta?.type),
      anchorBunchClass: safeString(candidate?.heuristic_meta?.anchor_bunch_class || candidate?.heuristicMeta?.anchorBunchClass),
      anchorBunchConfidence: safeNumber(candidate?.heuristic_meta?.anchor_bunch_confidence ?? candidate?.heuristicMeta?.anchorBunchConfidence, null),
      childCount: safeNumber(candidate?.heuristic_meta?.child_count ?? candidate?.heuristicMeta?.childCount, 0),
      weakChildCount: safeNumber(candidate?.heuristic_meta?.weak_child_count ?? candidate?.heuristicMeta?.weakChildCount, 0),
      strongChildCount: safeNumber(candidate?.heuristic_meta?.strong_child_count ?? candidate?.heuristicMeta?.strongChildCount, 0),
      weightedChildCount: safeNumber(candidate?.heuristic_meta?.weighted_child_count ?? candidate?.heuristicMeta?.weightedChildCount, null),
      ripeEvidenceScore: safeNumber(candidate?.heuristic_meta?.ripe_evidence_score ?? candidate?.heuristicMeta?.ripeEvidenceScore, null),
      unripeEvidenceScore: safeNumber(candidate?.heuristic_meta?.unripe_evidence_score ?? candidate?.heuristicMeta?.unripeEvidenceScore, null),
      dominantMaturity: safeString(candidate?.heuristic_meta?.dominant_maturity || candidate?.heuristicMeta?.dominantMaturity),
      score: safeNumber(candidate?.heuristic_meta?.score ?? candidate?.heuristicMeta?.score, null),
    },
    metrics: {
      boxArea: safeNumber(candidate?.metrics?.box_area, null),
      maskArea: safeNumber(candidate?.metrics?.mask_area, null),
      maskDensity: safeNumber(candidate?.metrics?.mask_density, null),
      redRatio: safeNumber(candidate?.metrics?.red_ratio, null),
      orangeRatio: safeNumber(candidate?.metrics?.orange_ratio, null),
      warmRatio: safeNumber(candidate?.metrics?.warm_ratio, null),
      greenYellowRatio: safeNumber(candidate?.metrics?.green_yellow_ratio, null),
    },
  };
}

function summarizeCandidates(candidates) {
  const rows = Array.isArray(candidates) ? candidates : [];
  const byPolicyStatus = {
    strong: 0,
    review: 0,
    color_corrected: 0,
    heuristic: 0,
    weak: 0,
    noise: 0,
    unknown: 0,
  };
  for (const row of rows) {
    const status = row.policyStatus || (row.weak ? "weak" : "strong");
    byPolicyStatus[status] = (byPolicyStatus[status] ?? 0) + 1;
  }

  return {
    total: rows.length,
    strong: byPolicyStatus.strong,
    review: byPolicyStatus.review,
    colorCorrected: byPolicyStatus.color_corrected,
    heuristics: byPolicyStatus.heuristic,
    weak: byPolicyStatus.weak,
    noise: byPolicyStatus.noise,
    byPolicyStatus,
    accepted: rows.filter((item) => item.accepted).length,
    ripeTomatoes: rows.filter((item) => item.category === "ripe_tomato").length,
    unripeTomatoes: rows.filter((item) => item.category === "unripe_tomato").length,
    ripeBunches: rows.filter((item) => item.category === "ripe_bunch").length,
    unripeBunches: rows.filter((item) => item.category === "unripe_bunch").length,
    mixedBunches: rows.filter((item) => item.category === "mixed_bunch").length,
    roiPass: rows.filter((item) => item.roi?.pass).length,
    rejectedOrSuppressed: rows.filter((item) => item.rejectReason || item.displaySuppressed).length,
  };
}

function isClusterCandidate(candidate) {
  return candidate?.category === "ripe_bunch" || candidate?.category === "unripe_bunch" || safeString(candidate?.label).toLowerCase().includes("bunch");
}

function isSingleTomatoCandidate(candidate) {
  return candidate?.category === "ripe_tomato" || candidate?.category === "unripe_tomato";
}

function buildRoiInsights(finalCandidates, rawCandidates, key) {
  const source = finalCandidates.length ? finalCandidates : rawCandidates;
  const clusters = source.filter((item) => isClusterCandidate(item) && item.bbox?.valid);
  const tomatoes = source.filter((item) => isSingleTomatoCandidate(item) && item.bbox?.valid);

  return clusters.map((cluster, index) => {
    const children = tomatoes.filter((tomato) => {
      if (tomato.id === cluster.id) return false;
      const center = boxCenter(tomato.bbox);
      return pointInsideBox(center, cluster.bbox) || boxIntersectionRatio(tomato.bbox, cluster.bbox) >= 0.15;
    });

    const ripeScore = children
      .filter((item) => item.category === "ripe_tomato")
      .reduce((sum, item) => sum + safeNumber(item.confidence, 0), 0);
    const unripeScore = children
      .filter((item) => item.category === "unripe_tomato")
      .reduce((sum, item) => sum + safeNumber(item.confidence, 0), 0);
    const strongCount = children.filter((item) => !item.weak).length;
    const weakCount = children.filter((item) => item.weak).length;
    const totalScore = ripeScore + unripeScore;
    const ripePct = totalScore > 0 ? Math.round((ripeScore / totalScore) * 100) : null;
    const unripePct = totalScore > 0 ? Math.round((unripeScore / totalScore) * 100) : null;
    const hasMixedMaturity = ripeScore > 0 && unripeScore > 0;
    const clusterLooksRipe = cluster.category === "ripe_bunch";
    const conflict =
      (clusterLooksRipe && unripeScore > ripeScore * 1.1) ||
      (!clusterLooksRipe && ripeScore > unripeScore * 1.1) ||
      (hasMixedMaturity && Math.min(ripeScore, unripeScore) / Math.max(ripeScore, unripeScore) > 0.45);

    let decision = "Uncertain";
    if (totalScore > 0) {
      if (Math.abs(ripeScore - unripeScore) / totalScore < 0.25) decision = "Mixed maturity";
      else decision = ripeScore > unripeScore ? "Mostly ripe" : "Mostly unripe";
    } else if (cluster.category === "ripe_bunch") {
      decision = "Cluster predicted ripe";
    } else if (cluster.category === "unripe_bunch") {
      decision = "Cluster predicted unripe";
    }

    return {
      id: `${key}:roi:${index}`,
      clusterBoxId: cluster.id,
      clusterLabel: cluster.categoryLabel,
      clusterConfidencePct: cluster.confidencePct,
      decision,
      conflict,
      hasMixedMaturity,
      childCount: children.length,
      strongCount,
      weakCount,
      ripeScore,
      unripeScore,
      ripePct,
      unripePct,
      childBoxIds: children.map((item) => item.id),
      note: conflict
        ? "Cluster label and inner tomato evidence are not fully aligned. Recommended for model review."
        : children.length
          ? "ROI estimate is based on child tomato detections inside the cluster box."
          : "No child tomato detections were found inside this cluster ROI.",
    };
  });
}

function buildReviewTags(finalCandidates, rawCandidates, roiInsights) {
  const tags = [];
  if (finalCandidates.some((item) => item.policyStatus === "heuristic")) tags.push("heuristics");
  if (finalCandidates.some((item) => item.policyStatus === "review")) tags.push("review candidates");
  if (finalCandidates.some((item) => item.policyStatus === "color_corrected")) tags.push("color corrected");
  if (finalCandidates.some((item) => item.policyStatus === "noise")) tags.push("noise detections");
  if (finalCandidates.some((item) => item.weak || item.policyStatus === "weak")) tags.push("weak detections");
  if (rawCandidates.length > finalCandidates.length) tags.push("raw candidates available");
  if (finalCandidates.some((item) => item.roi?.pass) || rawCandidates.some((item) => item.roi?.pass)) tags.push("ROI pass");
  if (roiInsights.some((item) => item.conflict)) tags.push("ROI conflict");
  if (roiInsights.some((item) => item.hasMixedMaturity)) tags.push("mixed maturity");
  if (finalCandidates.some((item) => item.rejectReason) || rawCandidates.some((item) => item.rejectReason)) tags.push("has rejection reasons");
  return tags;
}

function buildDetectionEvidence(row, rawRow, eventIndex, sessionId) {
  const key = evidenceKeyFromRow(row ?? rawRow);
  const imagePath = normalizeRelPath(row?.image_path || row?.annotated_image_path);
  const annotatedImagePath = normalizeRelPath(row?.annotated_image_path || row?.image_path);
  const rawImagePath = normalizeRelPath(row?.raw_image_path || rawRow?.raw_image_path);
  const finalCandidates = (Array.isArray(row?.detections) ? row.detections : []).map((candidate, index) =>
    normalizeCandidate(candidate, index, "final", key),
  );
  const rawCandidates = (Array.isArray(rawRow?.raw_candidates) ? rawRow.raw_candidates : []).map((candidate, index) =>
    normalizeCandidate(candidate, index, "raw", key),
  );
  const roiInsights = buildRoiInsights(finalCandidates, rawCandidates, key);

  return {
    key,
    eventIndex,
    eventType: row?.event_type ?? rawRow?.event_type ?? "detection",
    timestampMs: safeNumber(row?.timestamp_ms ?? rawRow?.timestamp_ms),
    timestampLocal: row?.timestamp_local ?? rawRow?.timestamp_local ?? null,
    image: {
      path: imagePath,
      url: sessionAssetUrl(sessionId, imagePath),
      annotatedPath: annotatedImagePath,
      annotatedUrl: sessionAssetUrl(sessionId, annotatedImagePath),
      rawPath: rawImagePath,
      rawUrl: sessionAssetUrl(sessionId, rawImagePath),
    },
    frame: {
      width: safeNumber(row?.frame?.width, 1280),
      height: safeNumber(row?.frame?.height, 720),
      channels: safeNumber(row?.frame?.channels, 3),
      timestampMs: safeNumber(row?.frame?.timestamp_ms, null),
      acceptedCount: safeNumber(row?.accepted_count, 0),
      weakCount: safeNumber(row?.weak_count, 0),
      rejectedCount: safeNumber(row?.rejected_count, 0),
      rawCandidateCount: safeNumber(rawRow?.raw_candidate_count, rawCandidates.length),
    },
    cameraView: row?.camera_view ?? null,
    finalCandidates,
    rawCandidates,
    roiInsights,
    reviewTags: buildReviewTags(finalCandidates, rawCandidates, roiInsights),
    summary: {
      final: summarizeCandidates(finalCandidates),
      raw: summarizeCandidates(rawCandidates),
      roiCount: roiInsights.length,
      conflictCount: roiInsights.filter((item) => item.conflict).length,
      mixedCount: roiInsights.filter((item) => item.hasMixedMaturity).length,
    },
  };
}

function buildDetectionEvidenceMap(detectionEventRows, rawCandidateRows, sessionId) {
  const rawByKey = new Map();
  const rawByTimestamp = new Map();
  for (const row of rawCandidateRows) {
    const key = evidenceKeyFromRow(row);
    rawByKey.set(key, row);
    rawByTimestamp.set(safeNumber(row?.timestamp_ms), row);
  }

  const map = new Map();
  for (let index = 0; index < detectionEventRows.length; index += 1) {
    const row = detectionEventRows[index];
    const key = evidenceKeyFromRow(row);
    const rawRow = rawByKey.get(key) ?? rawByTimestamp.get(safeNumber(row?.timestamp_ms)) ?? null;
    map.set(key, buildDetectionEvidence(row, rawRow, index, sessionId));
  }

  for (const row of rawCandidateRows) {
    const key = evidenceKeyFromRow(row);
    if (!map.has(key)) map.set(key, buildDetectionEvidence(null, row, map.size, sessionId));
  }

  return map;
}

function normalizeDetectionEvent(row, eventIndex, sessionId, evidenceByKey = new Map()) {
  const mapPose = row?.map_pose ?? {};
  const detections = Array.isArray(row?.detections) ? row.detections : [];
  const annotatedImagePath = normalizeRelPath(row.annotated_image_path || row.image_path);
  const rawImagePath = normalizeRelPath(row.raw_image_path);
  const imagePath = normalizeRelPath(row.image_path);
  const evidenceKey = evidenceKeyFromRow(row);
  const evidence = evidenceByKey.get(evidenceKey) ?? null;
  const evidenceCameraView = evidence?.cameraView ?? null;
  const rowCameraView = row?.camera_view ?? null;
  const frameWidth = optionalNumber(row?.frame?.width, optionalNumber(evidence?.frame?.width, 1280)) || 1280;
  const frameHeight = optionalNumber(row?.frame?.height, optionalNumber(evidence?.frame?.height, 720)) || 720;
  const frameChannels = optionalNumber(row?.frame?.channels, optionalNumber(evidence?.frame?.channels, 3)) || 3;
  const frameTimestampMs = optionalNumber(row?.frame?.timestamp_ms, optionalNumber(evidence?.frame?.timestampMs, null));

  return detections
    .map((detection, detectionIndex) => {
      const projection = detection?.map_projection ?? {};
      if (!projection.valid) return null;

      const x = safeNumber(projection.x, NaN);
      const y = safeNumber(projection.y, NaN);
      if (!Number.isFinite(x) || !Number.isFinite(y)) return null;

      const confidence = safeNumber(detection.confidence, null);
      const weak = Boolean(detection.weak);
      const policyStatus = normalizePolicyStatus(detection, weak);
      const sourceType = safeString(detection?.source_type || detection?.sourceType, detection?.heuristic ? "heuristic" : "model");
      const label = detection.label ?? "unknown";
      const classId = safeNumber(detection.class_id, -1);
      const category = normalizeTomatoCategory(label, classId);
      const selectedFinalBoxId = evidence?.finalCandidates?.[detectionIndex]?.id ?? `${evidenceKey}:final:${detectionIndex}`;
      const bbox = normalizeBbox(detection.bbox);
      const imageCenter = normalizeImageCenter(detection?.image_center, bbox, frameWidth, frameHeight);
      const cameraView = normalizeCameraView(evidenceCameraView ?? rowCameraView, projection);

      return {
        id: `${safeNumber(row.timestamp_ms)}-${eventIndex}-${detectionIndex}`,
        evidenceKey,
        selectedFinalBoxId,
        eventDetectionIndex: detectionIndex,
        timestampMs: safeNumber(row.timestamp_ms),
        timestampLocal: row.timestamp_local ?? null,
        eventType: row.event_type ?? "detection",
        label,
        category,
        categoryLabel: categoryLabel(category),
        classId,
        confidence,
        confidencePct: confidence == null ? null : Math.round(confidence * 100),
        weak,
        sourceType,
        policyStatus,
        policyStatusLabel: policyStatusLabel(policyStatus),
        heuristic: policyStatus === "heuristic" || sourceType === "heuristic" || Boolean(detection.heuristic),
        reviewCandidate: policyStatus === "review" || Boolean(detection.review_candidate),
        reviewReason: safeString(detection.review_reason || detection.reviewReason),
        colorCorrectedByPolicy: policyStatus === "color_corrected" || Boolean(detection.color_corrected_by_policy),
        correctionReason: safeString(detection.correction_reason || detection.correctionReason),
        colorCorrectionScore: safeNumber(detection.color_correction_score ?? detection.colorCorrectionScore, null),
        quality: policyStatus,
        accepted: ["strong", "review", "color_corrected", "heuristic"].includes(policyStatus),
        clusterPromoted: Boolean(detection.cluster_promoted),
        trackId: safeNumber(detection.track_id, -1),
        bbox,
        imageCenter,
        cameraView,
        image: {
          path: imagePath,
          url: sessionAssetUrl(sessionId, imagePath),
          annotatedPath: annotatedImagePath,
          annotatedUrl: sessionAssetUrl(sessionId, annotatedImagePath),
          rawPath: rawImagePath,
          rawUrl: sessionAssetUrl(sessionId, rawImagePath),
        },
        frame: {
          width: frameWidth,
          height: frameHeight,
          channels: frameChannels,
          timestampMs: frameTimestampMs,
          acceptedCount: safeNumber(row.accepted_count),
          weakCount: safeNumber(row.weak_count),
          rejectedCount: safeNumber(row.rejected_count),
        },
        robotPose: {
          x: safeNumber(mapPose.robot_x),
          y: safeNumber(mapPose.robot_y),
          yawDeg: safeNumber(mapPose.robot_yaw_deg),
          distanceM: safeNumber(mapPose.robot_distance_m),
        },
        projection: {
          x,
          y,
          valid: true,
          approximate: Boolean(projection.approximate ?? true),
          method: projection.method ?? "unknown",
          methodLabel: normalizeProjectionMethod(projection.method),
          distanceM: safeNumber(projection.projection_distance_m, null),
          cameraPanLeftDeg: optionalNumber(projection.camera_pan_left_deg, null),
          bboxBearingDeg: safeNumber(projection.bbox_bearing_deg, null),
          mapBearingDeg: safeNumber(projection.map_bearing_deg, null),
        },
      };
    })
    .filter(Boolean);
}

function buildDetectionStats(detections, trail, summary) {
  const byCategory = {
    ripe_tomato: 0,
    unripe_tomato: 0,
    ripe_bunch: 0,
    unripe_bunch: 0,
    mixed_bunch: 0,
    unknown: 0,
  };
  const byPolicyStatus = {
    strong: 0,
    review: 0,
    color_corrected: 0,
    heuristic: 0,
    weak: 0,
    noise: 0,
    unknown: 0,
  };

  for (const detection of detections) {
    byCategory[detection.category] = (byCategory[detection.category] ?? 0) + 1;
    const status = detection.policyStatus || (detection.weak ? "weak" : "strong");
    byPolicyStatus[status] = (byPolicyStatus[status] ?? 0) + 1;
  }

  const timestamps = detections
    .map((item) => item.timestampMs)
    .filter((value) => Number.isFinite(value) && value > 0)
    .sort((a, b) => a - b);

  const firstDetection = detections.find((item) => item.timestampMs === timestamps[0]) ?? null;
  const lastDetection = detections.find((item) => item.timestampMs === timestamps.at(-1)) ?? null;
  const finalPose = trail.at(-1) ?? null;

  return {
    total: detections.length,
    byCategory,
    categoryLabels: {
      ripe_tomato: "Ripe tomatoes",
      unripe_tomato: "Unripe tomatoes",
      ripe_bunch: "Ripe bunches",
      unripe_bunch: "Unripe bunches",
      mixed_bunch: "Mixed bunches",
      unknown: "Unknown",
    },
    byPolicyStatus,
    strong: byPolicyStatus.strong,
    review: byPolicyStatus.review,
    colorCorrected: byPolicyStatus.color_corrected,
    heuristics: byPolicyStatus.heuristic,
    weak: byPolicyStatus.weak,
    noise: byPolicyStatus.noise,
    firstDetectionTime: firstDetection?.timestampLocal ?? null,
    firstDetectionTimestampMs: firstDetection?.timestampMs ?? null,
    lastDetectionTime: lastDetection?.timestampLocal ?? null,
    lastDetectionTimestampMs: lastDetection?.timestampMs ?? null,
    estimatedScannedDistanceM: safeNumber(
      finalPose?.distanceM,
      safeNumber(summary?.final_pose?.distance_m, 0),
    ),
  };
}


function coalesceNumber(...values) {
  for (const value of values) {
    const num = Number(value);
    if (Number.isFinite(num)) return num;
  }
  return null;
}

function normalizeEnvironmentRow(row, firstGasKohm = null) {
  const environment = row?.environment ?? row?.env ?? null;
  if (!environment || environment.valid === false) return null;

  const timestampMs = safeNumber(row.timestamp_ms ?? row.timestampMs, NaN);
  if (!Number.isFinite(timestampMs)) return null;

  const tempC = coalesceNumber(
    environment.temp_c,
    environment.tempC,
    environment.temperature_c,
    environment.temperatureC,
    environment.temperature,
  );
  const humidityPct = coalesceNumber(
    environment.humidity_pct,
    environment.humidityPct,
    environment.relative_humidity_pct,
    environment.humidity,
  );
  const pressureHpa = coalesceNumber(
    environment.pressure_hpa,
    environment.pressureHpa,
    environment.barometric_pressure_hpa,
    environment.pressure,
  );
  const gasKohm = coalesceNumber(
    environment.gas_kohm,
    environment.gasKohm,
    environment.gas_resistance_kohm,
    environment.gasResistanceKohm,
    environment.gas,
  );

  const gasDeltaPct =
    gasKohm != null && firstGasKohm != null && firstGasKohm !== 0
      ? ((gasKohm - firstGasKohm) / firstGasKohm) * 100
      : coalesceNumber(environment.gas_delta_pct, environment.gasDeltaPct, environment.gas_change_pct);

  if (tempC == null && humidityPct == null && pressureHpa == null && gasKohm == null && gasDeltaPct == null) {
    return null;
  }

  return {
    timestampMs,
    timestampLocal: row.timestamp_local ?? row.timestampLocal ?? null,
    valid: Boolean(environment.valid ?? true),
    fresh: Boolean(environment.fresh ?? false),
    tempC,
    humidityPct,
    pressureHpa,
    gasKohm,
    gasDeltaPct,
  };
}

function mean(values) {
  const nums = values.filter((value) => Number.isFinite(value));
  if (!nums.length) return null;
  return nums.reduce((sum, value) => sum + value, 0) / nums.length;
}

function minMax(values) {
  const nums = values.filter((value) => Number.isFinite(value));
  if (!nums.length) return { min: null, max: null, avg: null };
  return {
    min: Math.min(...nums),
    max: Math.max(...nums),
    avg: mean(nums),
  };
}

function buildEnvironmentTimeline(robotRows) {
  const baseRows = Array.isArray(robotRows) ? robotRows : [];
  const firstGasRow = baseRows
    .map((row) => row?.environment ?? row?.env ?? null)
    .map((environment) =>
      coalesceNumber(
        environment?.gas_kohm,
        environment?.gasKohm,
        environment?.gas_resistance_kohm,
        environment?.gasResistanceKohm,
        environment?.gas,
      ),
    )
    .find((value) => Number.isFinite(value));

  return baseRows
    .map((row) => normalizeEnvironmentRow(row, firstGasRow ?? null))
    .filter(Boolean)
    .sort((a, b) => a.timestampMs - b.timestampMs);
}

function buildEnvironmentStats(environmentTimeline) {
  const rows = Array.isArray(environmentTimeline) ? environmentTimeline : [];
  return {
    samples: rows.length,
    tempC: minMax(rows.map((row) => row.tempC)),
    humidityPct: minMax(rows.map((row) => row.humidityPct)),
    pressureHpa: minMax(rows.map((row) => row.pressureHpa)),
    gasKohm: minMax(rows.map((row) => row.gasKohm)),
    gasDeltaPct: minMax(rows.map((row) => row.gasDeltaPct)),
  };
}

const VIDEO_EXTS = new Set([".mp4", ".webm", ".mov", ".m4v", ".avi", ".mkv"]);

function videoMimeType(relativePath) {
  const ext = path.extname(normalizeRelPath(relativePath)).toLowerCase();
  switch (ext) {
    case ".webm":
      return "video/webm";
    case ".mov":
      return "video/quicktime";
    case ".m4v":
      return "video/x-m4v";
    case ".mp4":
      return "video/mp4";
    default:
      return "video/mp4";
  }
}

function scoreVideoCandidate(relPath) {
  const normalized = normalizeRelPath(relPath).toLowerCase();
  if (normalized.endsWith(".webm")) return 100;
  if (normalized.includes("browser") && normalized.endsWith(".mp4")) return 95;
  if (normalized.includes("h264") && normalized.endsWith(".mp4")) return 92;
  if (normalized.includes("web") && normalized.endsWith(".mp4")) return 90;
  if (normalized.endsWith(".mp4")) return 60;
  return 10;
}

async function listVideoFiles(sessionDir) {
  const videosDir = path.join(sessionDir, "videos");
  if (!(await pathIsDir(videosDir))) return [];

  const entries = await fs.readdir(videosDir, { withFileTypes: true });
  return entries
    .filter((entry) => entry.isFile() && VIDEO_EXTS.has(path.extname(entry.name).toLowerCase()))
    .map((entry) => `videos/${entry.name}`);
}

async function resolveVideoPath(sessionDir, manifest) {
  const candidates = [];
  const manifestVideo = normalizeRelPath(manifest?.paths?.video);

  if (manifestVideo && (await fileExists(path.join(sessionDir, manifestVideo)))) {
    candidates.push(manifestVideo);
  }

  candidates.push(...(await listVideoFiles(sessionDir)));

  const unique = [...new Set(candidates.map(normalizeRelPath).filter(Boolean))];
  unique.sort((a, b) => scoreVideoCandidate(b) - scoreVideoCandidate(a) || a.localeCompare(b));
  return unique[0] ?? "";
}

function hasLikelyBrowserVideo(videoPath) {
  const normalized = normalizeRelPath(videoPath).toLowerCase();
  return (
    normalized.endsWith(".webm") ||
    normalized.includes("browser") ||
    normalized.includes("h264") ||
    normalized.includes("web")
  );
}

export function getDashboardRosMapSources() {
  const sessionRoot = resolveSessionRoot();
  const configuredSessionDir = resolveConfiguredSessionDir();

  return {
    sessionRoot,
    configuredSessionDir,
    expectedFolderShape:
      "src/session-data/session_YYYYMMDD_HHMMSS/{map_overlay_summary.json,map_pose_timeline.jsonl,detections_on_map.jsonl,ros2_map/.../map.yaml,map.pgm,videos/...mp4,images_ok/...jpg}",
  };
}

export async function buildDashboardRosMapPayload(sessionId = "") {
  const sessionDir = await findSessionDir(sessionId);
  if (!sessionDir) return null;

  const resolvedSessionId = path.basename(sessionDir);
  const summaryPath = path.join(sessionDir, "map_overlay_summary.json");
  const posePath = path.join(sessionDir, "map_pose_timeline.jsonl");
  const detectionsPath = path.join(sessionDir, "detections_on_map.jsonl");
  const latestMapPath = path.join(sessionDir, "ros2_map", "latest_map.json");
  const manifestPath = path.join(sessionDir, "session_manifest.json");
  const robotTimelinePath = path.join(sessionDir, "robot_timeline.jsonl");
  const detectionEventsPath = path.join(sessionDir, "detection_events.jsonl");
  const rawCandidatesPath = path.join(sessionDir, "raw_candidates.jsonl");

  const summary = await readJson(summaryPath);
  const latestMap = (await fileExists(latestMapPath)) ? await readJson(latestMapPath) : null;
  const manifest = (await fileExists(manifestPath)) ? await readJson(manifestPath) : null;

  let yamlPath = null;
  let pgmPath = null;

  const preliminaryAssets = resolveMapAssets(sessionDir, summary, { image: "map.pgm" });
  yamlPath = preliminaryAssets.yamlPath;

  if (!yamlPath || !(await fileExists(yamlPath))) {
    throw new Error(`Could not find map.yaml for dashboard session ${resolvedSessionId}.`);
  }

  const yaml = parseRosMapYaml(await fs.readFile(yamlPath, "utf8"));
  const finalAssets = resolveMapAssets(sessionDir, summary, yaml);
  yamlPath = finalAssets.yamlPath;
  pgmPath = finalAssets.pgmPath;

  if (!pgmPath || !(await fileExists(pgmPath))) {
    throw new Error(`Could not find map.pgm for dashboard session ${resolvedSessionId}.`);
  }

  const pgm = parsePgm(await fs.readFile(pgmPath));
  const poseRows = await readJsonLines(posePath);
  const trail = poseRows.map(poseFromRow).filter(Boolean).slice(-MAX_TRAIL_POINTS);
  const detectionRows = await readJsonLines(detectionsPath);
  const detectionEventRows = await readJsonLines(detectionEventsPath);
  const rawCandidateRows = await readJsonLines(rawCandidatesPath);
  const evidenceByKey = buildDetectionEvidenceMap(detectionEventRows, rawCandidateRows, resolvedSessionId);
  const detections = detectionRows
    .flatMap((row, eventIndex) => normalizeDetectionEvent(row, eventIndex, resolvedSessionId, evidenceByKey))
    .slice(-MAX_DETECTION_MARKERS)
    .sort((a, b) => a.timestampMs - b.timestampMs);
  const evidenceEvents = [...evidenceByKey.values()].sort((a, b) => a.timestampMs - b.timestampMs);
  const robotRows = await readJsonLines(robotTimelinePath);
  const environmentTimeline = buildEnvironmentTimeline(robotRows);
  const environmentStats = buildEnvironmentStats(environmentTimeline);

  const finalPose = trail.at(-1) ?? null;
  const firstPose = trail[0] ?? null;
  const resolutionM = safeNumber(yaml.resolution, safeNumber(summary?.map?.resolution_m, 0.05));
  const mapWidthM = pgm.width * resolutionM;
  const mapHeightM = pgm.height * resolutionM;
  const detectionStats = buildDetectionStats(detections, trail, summary);
  const videoPath = await resolveVideoPath(sessionDir, manifest);
  const hasPreparedPublicVideo = Boolean(videoPath) && (await publicSessionVideoExists(resolvedSessionId, videoPath));
  const preparedVideoUrl = hasPreparedPublicVideo ? publicSessionVideoUrl(resolvedSessionId, videoPath) : null;

  return {
    kind: "rbv2_ros2_slam_dashboard",
    schemaVersion: 2,
    session: {
      id: resolvedSessionId,
      dirName: resolvedSessionId,
      root: path.dirname(sessionDir),
      label: manifest?.started_at_local || sessionDateFromId(resolvedSessionId) || resolvedSessionId,
      startedAt: manifest?.started_at_local || null,
      stoppedAt: manifest?.stopped_at_local || null,
      stopReason: manifest?.stop_reason || summary?.stop_reason || null,
    },
    meta: {
      map_id: `${resolvedSessionId}-ros2-slam-map`,
      source: "ros2-slam-toolbox-session-files",
      coordinateFrame: "map-image-relative-meters-y-down",
      note:
        "Uses saved ROS2 slam_toolbox session files. Detection positions are estimated projections, not exact depth measurements.",
    },
    media: {
      videoPath,
      videoUrl: preparedVideoUrl || sessionAssetUrl(resolvedSessionId, videoPath),
      originalVideoUrl: sessionAssetUrl(resolvedSessionId, videoPath),
      preparedVideoUrl,
      publicPreparedPath: hasPreparedPublicVideo ? publicSessionVideoRelPath(resolvedSessionId, videoPath) : null,
      mimeType: preparedVideoUrl ? "video/mp4" : videoMimeType(videoPath),
      source: preparedVideoUrl ? "public/session-media" : "session-file-api",
      browserFriendlyNameRecommended: Boolean(videoPath) && !preparedVideoUrl && !hasLikelyBrowserVideo(videoPath),
      video: manifest?.video ?? null,
      note:
        Boolean(videoPath) && !preparedVideoUrl && !hasLikelyBrowserVideo(videoPath)
          ? "Run npm run prepare-dashboard-media before dev/build. It creates a browser-ready H.264 MP4 under public/session-media for Vercel and local hosting."
          : null,
    },
    map: {
      width: pgm.width,
      height: pgm.height,
      resolutionM,
      widthM: mapWidthM,
      heightM: mapHeightM,
      origin: yaml.origin,
      yaml: {
        image: yaml.image,
        mode: yaml.mode,
        negate: yaml.negate,
        occupiedThresh: yaml.occupiedThresh,
        freeThresh: yaml.freeThresh,
      },
      image: {
        format: pgm.magic,
        maxValue: pgm.maxValue,
        encoding: "base64-u8-grayscale",
        data: pgm.pixels.toString("base64"),
      },
      paths: {
        summary: path.relative(sessionDir, summaryPath).replaceAll("\\", "/"),
        poseTimeline: path.relative(sessionDir, posePath).replaceAll("\\", "/"),
        detectionsOnMap: path.relative(sessionDir, detectionsPath).replaceAll("\\", "/"),
        latestMap: latestMap ? path.relative(sessionDir, latestMapPath).replaceAll("\\", "/") : null,
        manifest: manifest ? path.relative(sessionDir, manifestPath).replaceAll("\\", "/") : null,
        yaml: path.relative(sessionDir, yamlPath).replaceAll("\\", "/"),
        pgm: path.relative(sessionDir, pgmPath).replaceAll("\\", "/"),
      },
    },
    summary: {
      stopReason: summary?.stop_reason ?? null,
      mapAssetsCopied: Boolean(summary?.map_assets_copied),
      manualStart: summary?.manual_start ?? null,
      firstPose: summary?.first_pose ?? firstPose,
      finalPose: summary?.final_pose ?? finalPose,
      counts: summary?.counts ?? manifest?.counts ?? null,
      latestMap,
    },
    robot: {
      pose: finalPose
        ? {
            x: finalPose.x,
            y: finalPose.y,
            yaw_deg: finalPose.yawDeg,
            distance_m: finalPose.distanceM,
            timestamp_ms: finalPose.timestampMs,
            timestamp_local: finalPose.timestampLocal,
          }
        : null,
    },
    timeline: {
      startTimestampMs: trail[0]?.timestampMs ?? detections[0]?.timestampMs ?? null,
      endTimestampMs: trail.at(-1)?.timestampMs ?? detections.at(-1)?.timestampMs ?? null,
      points: trail.length,
    },
    trail,
    detections,
    evidenceEvents,
    environment: {
      timeline: environmentTimeline,
      stats: environmentStats,
    },
    stats: {
      trailPoints: trail.length,
      detectionEvents: detectionRows.length,
      detectionMarkers: detections.length,
      evidenceEvents: evidenceEvents.length,
      acceptedDetections: detectionStats.strong,
      weakDetections: detectionStats.weak,
      finalDistanceM: detectionStats.estimatedScannedDistanceM,
      detections: detectionStats,
    },
  };
}
