import path from "node:path";
import { promises as fs } from "node:fs";

const DEFAULT_SESSION_ROOT = path.join(process.cwd(), "src", "session-data");
const SESSION_ID_PATTERN = /^session_[A-Za-z0-9_-]+$/;
const TRACK_CORRELATION_METHOD = "saved-yolo-track-id-primary";

const CLASS_LEGEND = [
  {
    key: "ripe_bunch",
    classIds: [0],
    label: "Ripe bunch",
    color: "#7e22ce",
    maturityScore: 1,
  },
  {
    key: "ripe_tomato",
    classIds: [1],
    label: "Ripe tomato",
    color: "#dc2626",
    maturityScore: 0.9,
  },
  {
    key: "unripe_tomato",
    classIds: [2],
    label: "Unripe tomato",
    color: "#22c55e",
    maturityScore: 0.1,
  },
  {
    key: "unripe_bunch",
    classIds: [3],
    label: "Unripe bunch",
    color: "#f97316",
    maturityScore: 0,
  },
  {
    key: "mixed_bunch",
    classIds: [4],
    label: "Mixed bunch",
    color: "#fde047",
    maturityScore: 0.5,
  },
  {
    key: "unknown",
    classIds: [],
    label: "Unknown",
    color: "#94a3b8",
    maturityScore: 0.5,
  },
];

function finite(value, fallback = null) {
  if (value == null || value === "") return fallback;
  const number = Number(value);
  return Number.isFinite(number) ? number : fallback;
}

function nonEmptyString(value, fallback = "") {
  return typeof value === "string" && value.trim() ? value : fallback;
}


function policyStatusOfDetection(detection) {
  const explicit = nonEmptyString(detection?.policy_status ?? detection?.policyStatus).toLowerCase();
  if (["strong", "review", "color_corrected", "heuristic", "weak", "noise"].includes(explicit)) return explicit;
  if (detection?.heuristic === true || detection?.source_type === "heuristic") return "heuristic";
  if (detection?.color_corrected_by_policy === true) return "color_corrected";
  if (detection?.review_candidate === true) return "review";
  const reason = nonEmptyString(detection?.reject_reason).toLowerCase();
  if (detection?.weak === true && reason.includes("noise")) return "noise";
  return detection?.weak === true ? "weak" : "strong";
}

function normalizeRelativePath(value) {
  return nonEmptyString(value)
    .replaceAll("\\", "/")
    .replaceAll("//", "/")
    .replace(/^\.\//, "")
    .replace(/^\/+/, "");
}

function resolveSessionRoot() {
  return process.env.ROBOT_DASHBOARD_SESSION_ROOT || DEFAULT_SESSION_ROOT;
}

function sanitizeSessionId(sessionId) {
  const clean = nonEmptyString(sessionId).trim();
  return SESSION_ID_PATTERN.test(clean) ? clean : "";
}

async function pathIsDirectory(targetPath) {
  try {
    return (await fs.stat(targetPath)).isDirectory();
  } catch {
    return false;
  }
}

async function fileExists(targetPath) {
  try {
    return (await fs.stat(targetPath)).isFile();
  } catch {
    return false;
  }
}

async function readJson(targetPath, fallback = null) {
  try {
    return JSON.parse(await fs.readFile(targetPath, "utf8"));
  } catch {
    return fallback;
  }
}

async function readJsonLines(targetPath) {
  if (!(await fileExists(targetPath))) return [];

  const text = await fs.readFile(targetPath, "utf8");

  return text
    .split(/\r?\n/)
    .map((line) => line.trim())
    .filter(Boolean)
    .filter((line) => !line.startsWith("version https://git-lfs.github.com/spec/"))
    .flatMap((line) => {
      try {
        return [JSON.parse(line)];
      } catch {
        return [];
      }
    });
}


function parseYamlScalar(value) {
  const trimmed = String(value ?? "").trim();
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
  const fields = {};

  String(text ?? "")
    .split(/\r?\n/)
    .forEach((rawLine) => {
      const line = rawLine.replace(/#.*$/, "").trim();
      if (!line) return;
      const colon = line.indexOf(":");
      if (colon < 0) return;
      fields[line.slice(0, colon).trim()] = parseYamlScalar(line.slice(colon + 1));
    });

  const originValues = String(fields.origin ?? "")
    .replace(/[\[\]]/g, "")
    .split(",")
    .map((value) => finite(value))
    .filter((value) => value != null);

  return {
    image: nonEmptyString(fields.image, "map.pgm"),
    mode: nonEmptyString(fields.mode, "trinary"),
    resolutionM: finite(fields.resolution, 0.05),
    origin: {
      x: finite(originValues[0], 0),
      y: finite(originValues[1], 0),
      yawRad: finite(originValues[2], 0),
    },
    negate: finite(fields.negate, 0),
    occupiedThreshold: finite(fields.occupied_thresh, 0.65),
    freeThreshold: finite(fields.free_thresh, 0.25),
  };
}

function skipPgmWhitespaceAndComments(buffer, offset) {
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
  let current = skipPgmWhitespaceAndComments(buffer, offset);
  const start = current;

  while (current < buffer.length) {
    const byte = buffer[current];
    if (byte === 9 || byte === 10 || byte === 13 || byte === 32 || byte === 35) break;
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
    throw new Error(`Unsupported ROS2 map PGM format: ${magic || "<empty>"}.`);
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
    throw new Error("Invalid ROS2 map PGM width or height.");
  }
  if (!Number.isFinite(maxValue) || maxValue <= 0) {
    throw new Error("Invalid ROS2 map PGM pixel range.");
  }

  const pixelCount = width * height;
  if (magic === "P5") {
    const pixelStart = skipPgmWhitespaceAndComments(buffer, offset);
    const bytesPerPixel = maxValue > 255 ? 2 : 1;
    const requiredLength = pixelCount * bytesPerPixel;
    const source = buffer.subarray(pixelStart, pixelStart + requiredLength);

    if (source.length < requiredLength) {
      throw new Error("ROS2 map PGM is shorter than its declared dimensions.");
    }

    const pixels = Buffer.alloc(pixelCount);
    if (bytesPerPixel === 1) {
      source.copy(pixels, 0, 0, pixelCount);
    } else {
      for (let index = 0; index < pixelCount; index += 1) {
        const rawValue = source.readUInt16BE(index * 2);
        pixels[index] = Math.round((rawValue / maxValue) * 255);
      }
    }

    return { magic, width, height, maxValue, pixels };
  }

  const values = buffer
    .toString("ascii", offset)
    .replace(/#[^\n\r]*/g, " ")
    .trim()
    .split(/\s+/)
    .map((value) => Number(value))
    .filter((value) => Number.isFinite(value));

  if (values.length < pixelCount) {
    throw new Error("ASCII ROS2 map PGM is shorter than its declared dimensions.");
  }

  const pixels = Buffer.alloc(pixelCount);
  for (let index = 0; index < pixelCount; index += 1) {
    pixels[index] = Math.round((values[index] / maxValue) * 255);
  }

  return { magic, width, height, maxValue, pixels };
}

function safeSessionRelativePath(value) {
  const normalized = normalizeRelativePath(value);
  return normalized && !normalized.includes("..") ? normalized : "";
}

async function resolveRosMapAssets(sessionDir, summary) {
  const copiedYaml = safeSessionRelativePath(summary?.paths?.copied_map_yaml);
  const copiedPgm = safeSessionRelativePath(summary?.paths?.copied_map_pgm);
  const copiedMapDir = safeSessionRelativePath(summary?.paths?.copied_map_session_dir);

  let yamlPath = copiedYaml ? path.join(sessionDir, copiedYaml) : null;
  if (!yamlPath || !(await fileExists(yamlPath))) {
    const directoryName = path.basename(copiedMapDir || "");
    if (directoryName) {
      yamlPath = path.join(sessionDir, "ros2_map", "map_session", directoryName, "map.yaml");
    }
  }

  if (!yamlPath || !(await fileExists(yamlPath))) return null;

  const yaml = parseRosMapYaml(await fs.readFile(yamlPath, "utf8"));
  let pgmPath = copiedPgm ? path.join(sessionDir, copiedPgm) : null;
  if (!pgmPath || !(await fileExists(pgmPath))) {
    pgmPath = path.join(path.dirname(yamlPath), path.basename(yaml.image || "map.pgm"));
  }

  if (!(await fileExists(pgmPath))) return null;

  const pgm = parsePgm(await fs.readFile(pgmPath));
  const resolutionM = finite(yaml.resolutionM, finite(summary?.map?.resolution_m, 0.05));

  return {
    valid: true,
    source: "saved-ros2-slam-toolbox-map",
    coordinateFrame: "map-image-relative-meters-y-down",
    width: pgm.width,
    height: pgm.height,
    resolutionM,
    widthM: pgm.width * resolutionM,
    heightM: pgm.height * resolutionM,
    origin: yaml.origin,
    yaml: {
      image: yaml.image,
      mode: yaml.mode,
      negate: yaml.negate,
      occupiedThreshold: yaml.occupiedThreshold,
      freeThreshold: yaml.freeThreshold,
    },
    image: {
      format: pgm.magic,
      maxValue: pgm.maxValue,
      encoding: "base64-u8-grayscale",
      data: pgm.pixels.toString("base64"),
    },
    paths: {
      yaml: path.relative(sessionDir, yamlPath).replaceAll("\\", "/"),
      pgm: path.relative(sessionDir, pgmPath).replaceAll("\\", "/"),
    },
  };
}

function sessionAssetUrl(sessionId, relativePath) {
  const clean = normalizeRelativePath(relativePath);
  if (!sessionId || !clean) return null;

  return `/api/session-file?session=${encodeURIComponent(sessionId)}&path=${encodeURIComponent(clean)}`;
}

function tomatoClass(label, classId) {
  const raw = nonEmptyString(label, "unknown").toLowerCase().replaceAll("_", " ");
  const isMixed = raw.includes("mixed") || classId === 4;
  const isBunch = raw.includes("bunch") || raw.includes("cluster") || classId === 0 || classId === 3 || isMixed;
  const isUnripe = raw.includes("unripe") || raw.includes("green") || classId === 2 || classId === 3;
  const isRipe = raw.includes("ripe") || raw.includes("turning") || classId === 0 || classId === 1;

  let key = "unknown";
  if (isBunch && isMixed) key = "mixed_bunch";
  else if (isBunch && isUnripe) key = "unripe_bunch";
  else if (isBunch && isRipe) key = "ripe_bunch";
  else if (isUnripe) key = "unripe_tomato";
  else if (isRipe) key = "ripe_tomato";

  return CLASS_LEGEND.find((item) => item.key === key) ?? CLASS_LEGEND.at(-1);
}

function normalizeBbox(bbox) {
  const x = finite(bbox?.x);
  const y = finite(bbox?.y);
  const w = finite(bbox?.w);
  const h = finite(bbox?.h);
  const valid = x != null && y != null && w != null && h != null && w > 0 && h > 0;

  return {
    valid,
    x: valid ? x : null,
    y: valid ? y : null,
    w: valid ? w : null,
    h: valid ? h : null,
  };
}

function classVoteKey(item) {
  return item.classKey || "unknown";
}

function weightedMean(items, valueSelector) {
  let numerator = 0;
  let denominator = 0;

  for (const item of items) {
    const value = valueSelector(item);
    const weight = Math.max(finite(item.confidence, 0.35), 0.1);

    if (Number.isFinite(value)) {
      numerator += value * weight;
      denominator += weight;
    }
  }

  return denominator > 0 ? numerator / denominator : null;
}

function average(items, valueSelector) {
  const values = items.map(valueSelector).filter((value) => Number.isFinite(value));
  if (!values.length) return null;
  return values.reduce((sum, value) => sum + value, 0) / values.length;
}

function pickClassFromObservations(observations) {
  const votes = new Map();

  observations.forEach((observation) => {
    const key = classVoteKey(observation);
    const weight = Math.max(finite(observation.confidence, 0.35), 0.1);
    votes.set(key, (votes.get(key) ?? 0) + weight);
  });

  const winner = [...votes.entries()].sort((a, b) => b[1] - a[1])[0]?.[0] ?? "unknown";
  return CLASS_LEGEND.find((item) => item.key === winner) ?? CLASS_LEGEND.at(-1);
}

function pickRepresentative(observations) {
  return observations
    .slice()
    .sort((a, b) => {
      const confidenceDifference = (b.confidence ?? 0) - (a.confidence ?? 0);
      if (confidenceDifference !== 0) return confidenceDifference;
      return (b.timestampMs ?? 0) - (a.timestampMs ?? 0);
    })[0] ?? null;
}

function normalizeEnvironmentRows(rows) {
  const sorted = rows
    .map((row) => {
      const environment = row?.environment ?? row?.env ?? {};
      return {
        timestampMs: finite(row?.timestamp_ms ?? row?.timestampMs),
        timestampLocal: row?.timestamp_local ?? row?.timestampLocal ?? null,
        tempC: finite(environment?.temp_c ?? environment?.tempC ?? environment?.temperature),
        humidityPct: finite(environment?.humidity_pct ?? environment?.humidityPct ?? environment?.humidity),
        pressureHpa: finite(environment?.pressure_hpa ?? environment?.pressureHpa ?? environment?.pressure),
        gasKohm: finite(
          environment?.gas_kohm ??
            environment?.gasKohm ??
            environment?.gas_resistance_kohm ??
            environment?.gasResistanceKohm ??
            environment?.gas,
        ),
        valid: environment?.valid !== false,
      };
    })
    .filter(
      (row) =>
        row.timestampMs != null &&
        [row.tempC, row.humidityPct, row.pressureHpa, row.gasKohm].some(
          (value) => value != null,
        ),
    )
    .sort((a, b) => a.timestampMs - b.timestampMs);

  const firstTimestamp = sorted[0]?.timestampMs ?? null;

  return sorted.map((row, index) => ({
    ...row,
    index,
    tSec: firstTimestamp == null ? index : Math.max(0, (row.timestampMs - firstTimestamp) / 1000),
  }));
}

function environmentStats(rows) {
  const statsFor = (key) => {
    const values = rows.map((row) => row[key]).filter((value) => Number.isFinite(value));
    if (!values.length) return { min: null, max: null, avg: null };

    return {
      min: Math.min(...values),
      max: Math.max(...values),
      avg: values.reduce((sum, value) => sum + value, 0) / values.length,
    };
  };

  return {
    samples: rows.length,
    tempC: statsFor("tempC"),
    humidityPct: statsFor("humidityPct"),
    pressureHpa: statsFor("pressureHpa"),
    gasKohm: statsFor("gasKohm"),
  };
}

function normalizeRobotTrail(rows) {
  return rows
    .map((row) => {
      const pose = row?.map_pose ?? row?.mapPose ?? {};
      const x = finite(pose?.robot_x ?? pose?.x);
      const y = finite(pose?.robot_y ?? pose?.y);

      if (x == null || y == null) return null;

      return {
        timestampMs: finite(row?.timestamp_ms ?? row?.timestampMs),
        timestampLocal: row?.timestamp_local ?? row?.timestampLocal ?? null,
        x,
        y,
        yawDeg: finite(pose?.robot_yaw_deg ?? pose?.yawDeg),
        distanceM: finite(pose?.robot_distance_m ?? pose?.distanceM),
      };
    })
    .filter(Boolean)
    .sort((a, b) => (a.timestampMs ?? 0) - (b.timestampMs ?? 0));
}

function normalizeTomatoObservations(rows, sessionId) {
  const observations = [];

  rows.forEach((row, eventIndex) => {
    const timestampMs = finite(row?.timestamp_ms ?? row?.timestampMs);
    const detections = Array.isArray(row?.detections) ? row.detections : [];
    const mapPose = row?.map_pose ?? {};
    const annotatedImagePath = normalizeRelativePath(
      row?.annotated_image_path ?? row?.image_path,
    );
    const rawImagePath = normalizeRelativePath(row?.raw_image_path);

    detections.forEach((detection, detectionIndex) => {
      const projection = detection?.map_projection ?? {};
      const x = finite(projection?.x);
      const y = finite(projection?.y);
      const policyStatus = policyStatusOfDetection(detection);
      const isAccepted =
        ["strong", "review", "color_corrected", "heuristic"].includes(policyStatus) &&
        detection?.valid !== false &&
        detection?.display_suppressed !== true;

      if (!isAccepted || projection?.valid !== true || x == null || y == null || timestampMs == null) {
        return;
      }

      const classId = finite(detection?.class_id, -1);
      const classInfo = tomatoClass(detection?.label, classId);
      const imagePath = annotatedImagePath || normalizeRelativePath(row?.image_path);

      observations.push({
        id: `${timestampMs}-${eventIndex}-${detectionIndex}`,
        timestampMs,
        timestampLocal: row?.timestamp_local ?? row?.timestampLocal ?? null,
        classId,
        classKey: classInfo.key,
        label: classInfo.label,
        sourceLabel: nonEmptyString(detection?.label, "unknown"),
        policyStatus,
        sourceType: nonEmptyString(detection?.source_type, "model"),
        heuristic: detection?.heuristic === true || policyStatus === "heuristic",
        reviewCandidate: detection?.review_candidate === true || policyStatus === "review",
        colorCorrectedByPolicy: detection?.color_corrected_by_policy === true || policyStatus === "color_corrected",
        color: classInfo.color,
        maturityScore: classInfo.maturityScore,
        confidence: finite(detection?.confidence, 0),
        trackId: finite(detection?.track_id, -1),
        trackHits: finite(detection?.track_hits, 0),
        trackStableBest: detection?.track_stable_best === true,
        x,
        y,
        bbox: normalizeBbox(detection?.bbox),
        imagePath,
        imageUrl: sessionAssetUrl(sessionId, imagePath),
        rawImagePath,
        rawImageUrl: sessionAssetUrl(sessionId, rawImagePath),
        projection: {
          method: nonEmptyString(projection?.method, "unknown"),
          approximate: projection?.approximate !== false,
          distanceM: finite(projection?.projection_distance_m),
          cameraPanLeftDeg: finite(projection?.camera_pan_left_deg),
          bboxBearingDeg: finite(projection?.bbox_bearing_deg),
          mapBearingDeg: finite(projection?.map_bearing_deg),
        },
        scanPose: {
          x: finite(mapPose?.robot_x),
          y: finite(mapPose?.robot_y),
          yawDeg: finite(mapPose?.robot_yaw_deg),
          distanceM: finite(mapPose?.robot_distance_m),
        },
      });
    });
  });

  return observations.sort((a, b) => a.timestampMs - b.timestampMs);
}

function correlationKeyForObservation(observation) {
  const trackId = finite(observation?.trackId, -1);

  /*
   * The exporter already supplies a tracker identity for strong accepted
   * detections. This must be the primary correlation signal: two distinct
   * tracker IDs can legitimately be very close on the ROS2 map or even occur
   * in the same camera frame. Spatial proximity is therefore never used to
   * collapse accepted strong tomatoes.
   */
  if (trackId >= 0) {
    return {
      key: `track:${trackId}`,
      method: "saved-track-id",
      trackId,
    };
  }

  /*
   * A strong detection without a tracker ID remains visible as its own
   * landmark. It is deliberately not nearest-neighbour merged with nearby
   * tomatoes, because that would hide a real accepted detection.
   */
  return {
    key: `untracked:${observation.id}`,
    method: "untracked-strong-observation",
    trackId: null,
  };
}

function clusterObservations(observations) {
  const groups = new Map();

  for (const observation of observations) {
    const correlation = correlationKeyForObservation(observation);
    const existing = groups.get(correlation.key);

    if (existing) {
      existing.observations.push(observation);
      continue;
    }

    groups.set(correlation.key, {
      correlation,
      observations: [observation],
    });
  }

  const landmarkByObservationId = new Map();
  let trackedLandmarkCount = 0;
  let untrackedLandmarkCount = 0;

  const landmarks = [...groups.values()]
    .sort((left, right) => {
      const leftTimestamp = Math.min(...left.observations.map((item) => item.timestampMs ?? Infinity));
      const rightTimestamp = Math.min(...right.observations.map((item) => item.timestampMs ?? Infinity));
      if (leftTimestamp !== rightTimestamp) return leftTimestamp - rightTimestamp;
      return left.correlation.key.localeCompare(right.correlation.key);
    })
    .map((group, index) => {
      const observationsInGroup = group.observations
        .slice()
        .sort((a, b) => a.timestampMs - b.timestampMs);
      const classInfo = pickClassFromObservations(observationsInGroup);
      const representative = pickRepresentative(observationsInGroup);
      const landmarkId = `landmark-${String(index + 1).padStart(3, "0")}`;
      const sourceTrackIds = [...new Set(
        observationsInGroup
          .map((item) => item.trackId)
          .filter((trackId) => Number.isFinite(trackId) && trackId >= 0),
      )];

      if (group.correlation.method === "saved-track-id") trackedLandmarkCount += 1;
      else untrackedLandmarkCount += 1;

      observationsInGroup.forEach((observation) => {
        landmarkByObservationId.set(observation.id, landmarkId);
      });

      return {
        id: landmarkId,
        x: weightedMean(observationsInGroup, (item) => item.x),
        y: weightedMean(observationsInGroup, (item) => item.y),
        classId: representative?.classId ?? -1,
        classKey: classInfo.key,
        label: classInfo.label,
        color: classInfo.color,
        maturityScore: classInfo.maturityScore,
        confidence: average(observationsInGroup, (item) => item.confidence),
        bestConfidence: Math.max(...observationsInGroup.map((item) => item.confidence ?? 0), 0),
        observationCount: observationsInGroup.length,
        sourceTrackIds,
        correlation: {
          method: group.correlation.method,
          trackId: group.correlation.trackId,
          key: group.correlation.key,
        },
        firstTimestampMs: observationsInGroup[0]?.timestampMs ?? null,
        firstTimestampLocal: observationsInGroup[0]?.timestampLocal ?? null,
        latestTimestampMs: observationsInGroup.at(-1)?.timestampMs ?? null,
        latestTimestampLocal: observationsInGroup.at(-1)?.timestampLocal ?? null,
        representative,
        observations: observationsInGroup,
      };
    });

  return {
    landmarks,
    observations: observations.map((observation) => ({
      ...observation,
      landmarkId: landmarkByObservationId.get(observation.id) ?? null,
    })),
    correlation: {
      method: TRACK_CORRELATION_METHOD,
      spatialMerging: false,
      trackedLandmarkCount,
      untrackedLandmarkCount,
    },
  };
}

function buildAnalysisExtent(landmarks, robotTrail, rosMap = null) {
  if (rosMap?.valid && Number.isFinite(rosMap.widthM) && Number.isFinite(rosMap.heightM)) {
    return {
      minX: 0,
      maxX: rosMap.widthM,
      minY: 0,
      maxY: rosMap.heightM,
      widthM: rosMap.widthM,
      heightM: rosMap.heightM,
      paddingM: 0,
      coordinateFrame: rosMap.coordinateFrame,
      source: "ros2-map-raster",
    };
  }
  const points = [
    ...landmarks.map((item) => ({ x: item.x, y: item.y })),
    ...robotTrail.map((item) => ({ x: item.x, y: item.y })),
  ].filter((point) => Number.isFinite(point.x) && Number.isFinite(point.y));

  if (!points.length) {
    return {
      minX: -1,
      maxX: 1,
      minY: -1,
      maxY: 1,
      widthM: 2,
      heightM: 2,
      paddingM: 0.4,
    };
  }

  let minX = Math.min(...points.map((point) => point.x));
  let maxX = Math.max(...points.map((point) => point.x));
  let minY = Math.min(...points.map((point) => point.y));
  let maxY = Math.max(...points.map((point) => point.y));

  const paddingM = Math.max(0.28, Math.max(maxX - minX, maxY - minY) * 0.18);
  minX -= paddingM;
  maxX += paddingM;
  minY -= paddingM;
  maxY += paddingM;

  const minimumSpanM = 1.35;
  if (maxX - minX < minimumSpanM) {
    const center = (minX + maxX) / 2;
    minX = center - minimumSpanM / 2;
    maxX = center + minimumSpanM / 2;
  }

  if (maxY - minY < minimumSpanM) {
    const center = (minY + maxY) / 2;
    minY = center - minimumSpanM / 2;
    maxY = center + minimumSpanM / 2;
  }

  return {
    minX,
    maxX,
    minY,
    maxY,
    widthM: maxX - minX,
    heightM: maxY - minY,
    paddingM,
  };
}

function countSourceDetections(detectionRows) {
  let accepted = 0;
  let weak = 0;
  let total = 0;

  detectionRows.forEach((row) => {
    const detections = Array.isArray(row?.detections) ? row.detections : [];
    total += detections.length;
    detections.forEach((detection) => {
      const status = policyStatusOfDetection(detection);
      if (status === "weak" || status === "noise") weak += 1;
      else if (["strong", "review", "color_corrected", "heuristic"].includes(status) && detection?.valid !== false && detection?.display_suppressed !== true) accepted += 1;
    });
  });

  return { total, accepted, weak };
}

async function buildSessionListItem(sessionRoot, id) {
  const directory = path.join(sessionRoot, id);
  const manifest = await readJson(path.join(directory, "session_manifest.json"), null);
  const summary = await readJson(path.join(directory, "map_overlay_summary.json"), null);
  const hasDetections = await fileExists(path.join(directory, "detections_on_map.jsonl"));
  const hasTimeline = await fileExists(path.join(directory, "robot_timeline.jsonl"));

  return {
    id,
    label: manifest?.started_at_local ?? summary?.session_id ?? id,
    startedAt: manifest?.started_at_local ?? null,
    stoppedAt: manifest?.stopped_at_local ?? null,
    hasDetections,
    hasM5Stick: hasTimeline,
    mapAvailable: summary?.map?.valid === true,
    counts: {
      detectionEvents: finite(manifest?.counts?.detections_on_map_events, 0),
      mapPoseRows: finite(manifest?.counts?.map_pose_rows, 0),
      timelineRows: finite(manifest?.counts?.timeline_rows, 0),
      acceptedImages: finite(manifest?.counts?.ok_images, 0),
    },
  };
}

export async function listDataAnalysisSessions() {
  const sessionRoot = resolveSessionRoot();
  if (!(await pathIsDirectory(sessionRoot))) return [];

  const entries = await fs.readdir(sessionRoot, { withFileTypes: true });
  const ids = entries
    .filter((entry) => entry.isDirectory() && SESSION_ID_PATTERN.test(entry.name))
    .map((entry) => entry.name);

  const sessions = await Promise.all(ids.map((id) => buildSessionListItem(sessionRoot, id)));
  return sessions.sort((a, b) => b.id.localeCompare(a.id));
}

export async function buildDataAnalysisSessionPayload(requestedSessionId = "") {
  const sessions = await listDataAnalysisSessions();
  const cleanRequestedId = sanitizeSessionId(requestedSessionId);
  const selectedSessionId =
    sessions.find((item) => item.id === cleanRequestedId)?.id ?? sessions[0]?.id ?? null;

  if (!selectedSessionId) {
    return {
      kind: "ecofarm_data_analysis_session",
      selectedSessionId: null,
      sessions: [],
      session: null,
      environment: { series: [], stats: { samples: 0 }, validity: { envPct: null, imuPct: null } },
      map: { landmarks: [], observations: [], robotTrail: [], layout: buildAnalysisExtent([], []), rosMap: null },
      classes: CLASS_LEGEND,
      quality: { message: "No session-data folders were found." },
    };
  }

  const sessionRoot = resolveSessionRoot();
  const sessionDir = path.join(sessionRoot, selectedSessionId);
  const [manifest, summary, robotTimelineRows, poseRows, detectionRows] = await Promise.all([
    readJson(path.join(sessionDir, "session_manifest.json"), null),
    readJson(path.join(sessionDir, "map_overlay_summary.json"), null),
    readJsonLines(path.join(sessionDir, "robot_timeline.jsonl")),
    readJsonLines(path.join(sessionDir, "map_pose_timeline.jsonl")),
    readJsonLines(path.join(sessionDir, "detections_on_map.jsonl")),
  ]);

  const rosMap = await resolveRosMapAssets(sessionDir, summary).catch((error) => ({
    valid: false,
    source: "ros2-map-load-failed",
    error: error instanceof Error ? error.message : String(error),
  }));
  const environmentSeries = normalizeEnvironmentRows(robotTimelineRows);
  const robotTrail = normalizeRobotTrail(poseRows);
  const rawObservations = normalizeTomatoObservations(detectionRows, selectedSessionId);
  const clustered = clusterObservations(rawObservations);
  const layout = buildAnalysisExtent(clustered.landmarks, robotTrail, rosMap);
  const sourceCounts = countSourceDetections(detectionRows);
  const latestTimestampMs = Math.max(
    ...robotTrail.map((item) => item.timestampMs ?? 0),
    ...clustered.observations.map((item) => item.timestampMs ?? 0),
    0,
  );

  return {
    kind: "ecofarm_data_analysis_session",
    selectedSessionId,
    sessions,
    session: {
      id: selectedSessionId,
      label: manifest?.started_at_local ?? selectedSessionId,
      startedAt: manifest?.started_at_local ?? null,
      stoppedAt: manifest?.stopped_at_local ?? null,
      stopReason: manifest?.stop_reason ?? summary?.stop_reason ?? null,
      latestTimestampMs,
    },
    classes: CLASS_LEGEND,
    environment: {
      series: environmentSeries,
      stats: environmentStats(environmentSeries),
      sampleCount: environmentSeries.length,
      totalEntries: robotTimelineRows.length,
      validity: {
        envPct:
          robotTimelineRows.length > 0
            ? environmentSeries.length / robotTimelineRows.length
            : null,
        imuPct: null,
      },
      gasAvailable: environmentSeries.some((item) => item.gasKohm != null),
    },
    map: {
      layout,
      robotTrail,
      observations: clustered.observations,
      landmarks: clustered.landmarks,
      correlation: clustered.correlation,
      rosMap,
      coordinateNote:
        "Tomato anchors use the saved detections_on_map.jsonl map projections. Strong detections are correlated by saved YOLO tracker ID; distinct tracker IDs are never spatially merged, even when their approximate map anchors are close.",
      sourceMap: {
        valid: summary?.map?.valid === true,
        resolutionM: finite(summary?.map?.resolution_m),
        originX: finite(summary?.map?.origin_x),
        originY: finite(summary?.map?.origin_y),
      },
    },
    quality: {
      sourceDetectionEvents: detectionRows.length,
      acceptedObservationCount: sourceCounts.accepted,
      weakObservationCount: sourceCounts.weak,
      totalSourceDetections: sourceCounts.total,
      groupedLandmarkCount: clustered.landmarks.length,
      trackedLandmarkCount: clustered.correlation.trackedLandmarkCount,
      untrackedStrongLandmarkCount: clustered.correlation.untrackedLandmarkCount,
      correlationMethod: clustered.correlation.method,
      hasM5Stick: environmentSeries.length > 0,
      gasAvailable: environmentSeries.some((item) => item.gasKohm != null),
      usesApproximateMapProjection: true,
    },
  };
}
