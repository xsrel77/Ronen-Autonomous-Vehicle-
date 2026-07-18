export const DEFAULT_SESSION_MAP_SETTINGS = {
  motionMetersPerCommandSecond: 0.01,
  turnDegreesPerCommandSecond: 0.05,
  headingHintBlend: 0.45,
  headingHintDirection: -1,
  cameraHorizontalFovDeg: 70,
  occupancyCellSizeM: 0.12,
  zoomedFrameMinimumSupportCount: 4,
  zoomDistanceToleranceM: 0.2,
  maxIntegrationSeconds: 1,

  mapRayMinDistanceM: 0.45,
  mapRayMaxDistanceM: 4.5,
  mapRayHalfWidthM: 0.16,
  mapRayWidthGrowthPerMeter: 0.04,
  mapRayBinSizeM: 0.16,
  mapRayMinimumPoints: 3,

  landmarkAssociationDistanceM: 0.18,
  trackAssociationDistanceM: 0.32,
};

function finite(value) {
  return typeof value === "number" && Number.isFinite(value) ? value : null;
}

function clamp(value, minimum, maximum) {
  return Math.max(minimum, Math.min(maximum, value));
}

function degToRad(degrees) {
  return (degrees * Math.PI) / 180;
}

function radToDeg(radians) {
  return (radians * 180) / Math.PI;
}

function normalizeRadians(value) {
  let radians = value;

  while (radians > Math.PI) radians -= Math.PI * 2;
  while (radians <= -Math.PI) radians += Math.PI * 2;

  return radians;
}

function circularLerpRadians(fromRadians, toRadians, amount) {
  const difference = normalizeRadians(toRadians - fromRadians);
  return normalizeRadians(fromRadians + difference * clamp(amount, 0, 1));
}

function mergeSettings(overrides = {}) {
  return {
    ...DEFAULT_SESSION_MAP_SETTINGS,
    ...overrides,
  };
}

function sortedTimelineRows(timeline) {
  if (!Array.isArray(timeline)) return [];

  return timeline
    .filter((row) => finite(row?.timestampMs) != null)
    .slice()
    .sort((a, b) => a.timestampMs - b.timestampMs);
}

function rowHeadingHint(row) {
  return finite(row?.headingHintDeg);
}

function firstFiniteHeadingHint(rows) {
  const first = rows.find((row) => rowHeadingHint(row) != null);
  return first ? rowHeadingHint(first) : null;
}

function nearestByTimestamp(items, timestampMs) {
  if (!Array.isArray(items) || !items.length) return null;
  if (!Number.isFinite(timestampMs)) return items.at(-1) ?? null;

  let nearest = items[0];
  let smallestDifference = Math.abs(nearest.timestampMs - timestampMs);

  for (let index = 1; index < items.length; index += 1) {
    const candidate = items[index];
    const difference = Math.abs(candidate.timestampMs - timestampMs);

    if (difference < smallestDifference) {
      nearest = candidate;
      smallestDifference = difference;
    }
  }

  return nearest;
}

export function findNearestEstimatedPose(poses, timestampMs) {
  return nearestByTimestamp(poses, timestampMs);
}

export function findNearestTimelineRow(timeline, timestampMs) {
  return nearestByTimestamp(sortedTimelineRows(timeline), timestampMs);
}

export function estimateSessionPoses(timeline, settingsOverrides = {}) {
  const settings = mergeSettings(settingsOverrides);
  const rows = sortedTimelineRows(timeline);

  if (!rows.length) return [];

  const referenceHeadingHintDeg = firstFiniteHeadingHint(rows);
  const firstRow = rows[0];
  const firstHint = rowHeadingHint(firstRow);

  let xM = 0;
  let yM = 0;
  let commandYawRad = 0;

  const firstHintYawRad =
    referenceHeadingHintDeg != null && firstHint != null
      ? degToRad(
          (firstHint - referenceHeadingHintDeg) * settings.headingHintDirection,
        )
      : null;

  let yawRad =
    firstHintYawRad == null
      ? commandYawRad
      : circularLerpRadians(
          commandYawRad,
          firstHintYawRad,
          settings.headingHintBlend,
        );

  const poses = [
    {
      timestampMs: firstRow.timestampMs,
      timestampLocal: firstRow.timestampLocal ?? null,
      xM,
      yM,
      yawRad,
      yawDeg: radToDeg(yawRad),
      headingHintDeg: firstHint,
      forwardSpeedCommand: finite(firstRow.forwardSpeed) ?? 0,
      steeringSpeedCommand: finite(firstRow.steeringSpeed) ?? 0,
      integrationSeconds: 0,
    },
  ];

  for (let index = 1; index < rows.length; index += 1) {
    const previousRow = rows[index - 1];
    const currentRow = rows[index];

    const rawDeltaSeconds =
      (currentRow.timestampMs - previousRow.timestampMs) / 1000;

    const deltaSeconds = clamp(
      Number.isFinite(rawDeltaSeconds) ? rawDeltaSeconds : 0,
      0,
      settings.maxIntegrationSeconds,
    );

    const forwardSpeedCommand = finite(previousRow.forwardSpeed) ?? 0;
    const steeringSpeedCommand = finite(previousRow.steeringSpeed) ?? 0;

    const turnDegrees =
      steeringSpeedCommand *
      settings.turnDegreesPerCommandSecond *
      deltaSeconds;

    commandYawRad = normalizeRadians(commandYawRad + degToRad(turnDegrees));

    const hint = rowHeadingHint(currentRow);
    const relativeHintYawRad =
      referenceHeadingHintDeg != null && hint != null
        ? degToRad(
            (hint - referenceHeadingHintDeg) * settings.headingHintDirection,
          )
        : null;

    yawRad =
      relativeHintYawRad == null
        ? commandYawRad
        : circularLerpRadians(
            commandYawRad,
            relativeHintYawRad,
            settings.headingHintBlend,
          );

    const linearDistanceM =
      forwardSpeedCommand *
      settings.motionMetersPerCommandSecond *
      deltaSeconds;

    xM += linearDistanceM * Math.cos(yawRad);
    yM += linearDistanceM * Math.sin(yawRad);

    poses.push({
      timestampMs: currentRow.timestampMs,
      timestampLocal: currentRow.timestampLocal ?? null,
      xM,
      yM,
      yawRad,
      yawDeg: radToDeg(yawRad),
      headingHintDeg: hint,
      forwardSpeedCommand,
      steeringSpeedCommand,
      integrationSeconds: deltaSeconds,
    });
  }

  return poses;
}

export function transformLidarPreviewPoints(lidarPreview, poses) {
  const sourcePoints = Array.isArray(lidarPreview?.points)
    ? lidarPreview.points
    : [];

  return sourcePoints
    .map((point, index) => {
      if (
        !Number.isFinite(point?.xM) ||
        !Number.isFinite(point?.yM) ||
        !Number.isFinite(point?.timestampMs)
      ) {
        return null;
      }

      const pose = findNearestEstimatedPose(poses, point.timestampMs);
      if (!pose) return null;

      const cosYaw = Math.cos(pose.yawRad);
      const sinYaw = Math.sin(pose.yawRad);

      return {
        id: `lidar-${point.timestampMs}-${index}`,
        timestampMs: point.timestampMs,
        localXM: point.xM,
        localYM: point.yM,
        distanceM: finite(point.distanceM),
        angleDeg: finite(point.angleDeg),
        estimatedXM: pose.xM + point.xM * cosYaw - point.yM * sinYaw,
        estimatedYM: pose.yM + point.xM * sinYaw + point.yM * cosYaw,
        poseTimestampMs: pose.timestampMs,
      };
    })
    .filter(Boolean);
}

function detectionFrameStats(event, detection) {
  const frameWidth = finite(event?.frame?.width);
  const frameHeight = finite(event?.frame?.height);
  const bboxX = finite(detection?.bbox?.x);
  const bboxY = finite(detection?.bbox?.y);
  const bboxW = finite(detection?.bbox?.w);
  const bboxH = finite(detection?.bbox?.h);

  if (
    frameWidth == null ||
    frameHeight == null ||
    bboxX == null ||
    bboxY == null ||
    bboxW == null ||
    bboxH == null ||
    frameWidth <= 0 ||
    frameHeight <= 0 ||
    bboxW <= 0 ||
    bboxH <= 0
  ) {
    return {
      frameWidth,
      frameHeight,
      boxCenterXRatio: null,
      boxCenterYRatio: null,
      boxWidthRatio: null,
      boxHeightRatio: null,
      boxAreaRatio: null,
    };
  }

  return {
    frameWidth,
    frameHeight,
    boxCenterXRatio: clamp((bboxX + bboxW / 2) / frameWidth, 0, 1),
    boxCenterYRatio: clamp((bboxY + bboxH / 2) / frameHeight, 0, 1),
    boxWidthRatio: clamp(bboxW / frameWidth, 0, 1),
    boxHeightRatio: clamp(bboxH / frameHeight, 0, 1),
    boxAreaRatio: clamp((bboxW * bboxH) / (frameWidth * frameHeight), 0, 1),
  };
}

function effectiveHorizontalFovDeg(event, settings) {
  const zoom = Math.max(finite(event?.camera?.digitalZoom) ?? 1, 1);
  return settings.cameraHorizontalFovDeg / zoom;
}

function correctedApparentSizeRatio(event, detection) {
  const stats = detectionFrameStats(event, detection);
  const zoom = Math.max(finite(event?.camera?.digitalZoom) ?? 1, 1);

  if (stats.boxWidthRatio == null && stats.boxAreaRatio == null) {
    return null;
  }

  const linearAreaRatio =
    stats.boxAreaRatio != null ? Math.sqrt(stats.boxAreaRatio) : null;

  const baseSize = Math.max(
    stats.boxWidthRatio ?? 0,
    linearAreaRatio ?? 0,
  );

  return clamp(baseSize * zoom, 0, 1);
}

function maximumReasonableProjectionDistanceM(event, detection, settings) {
  const apparentSizeRatio = correctedApparentSizeRatio(event, detection);

  if (apparentSizeRatio == null) {
    return settings.mapRayMaxDistanceM;
  }

  if (apparentSizeRatio >= 0.34) return 1.35;
  if (apparentSizeRatio >= 0.27) return 1.8;
  if (apparentSizeRatio >= 0.2) return 2.3;
  if (apparentSizeRatio >= 0.15) return 2.9;
  if (apparentSizeRatio >= 0.11) return 3.5;
  return settings.mapRayMaxDistanceM;
}

function classifyProjectionQuality({ mapSurface, event, detection, settings }) {
  if (!mapSurface || !Number.isFinite(mapSurface.distanceM)) {
    return { level: "low", score: 0 };
  }

  const zoom = Math.max(finite(event?.camera?.digitalZoom) ?? 1, 1);
  const maxDistance = maximumReasonableProjectionDistanceM(event, detection, settings);
  let score = 0;

  score += Math.min((mapSurface.supportCount ?? 0) / 6, 1) * 0.5;
  score += Math.max(0, 1 - (mapSurface.meanCrossM ?? 0) / 0.22) * 0.2;

  if (mapSurface.distanceM <= maxDistance + settings.zoomDistanceToleranceM) {
    score += 0.2;
  } else {
    score -= 0.25;
  }

  if (zoom <= 1.05) score += 0.1;
  else if ((mapSurface.supportCount ?? 0) >= settings.zoomedFrameMinimumSupportCount + 1) score += 0.05;

  if (score >= 0.72) return { level: "high", score };
  if (score >= 0.48) return { level: "medium", score };
  return { level: "low", score };
}

function cameraBearingRadians(event, detection, settings) {
  const direction = event?.camera?.directionRobot ?? {};
  const xForward = finite(direction.xForward);
  const yLeft = finite(direction.yLeft);
  const panRelativeDeg = finite(event?.camera?.panRelativeDeg);

  let bearingRad = null;

  if (xForward != null && yLeft != null) {
    bearingRad = Math.atan2(yLeft, xForward);
  } else if (panRelativeDeg != null) {
    bearingRad = degToRad(panRelativeDeg);
  }

  if (bearingRad == null) return null;

  const frame = detectionFrameStats(event, detection);

  if (frame.frameWidth == null || frame.boxCenterXRatio == null) {
    return bearingRad;
  }

  const horizontalOffsetDeg =
    (frame.boxCenterXRatio - 0.5) * effectiveHorizontalFovDeg(event, settings);

  return normalizeRadians(bearingRad + degToRad(horizontalOffsetDeg));
}

function normalizedBoxCenter(event, detection) {
  const stats = detectionFrameStats(event, detection);

  return {
    x: stats.boxCenterXRatio,
    y: stats.boxCenterYRatio,
  };
}

function distanceToMapSurface({ robotXM, robotYM, bearingRad }, lidarPoints, settings) {
  if (!Array.isArray(lidarPoints) || !lidarPoints.length) return null;

  const directionX = Math.cos(bearingRad);
  const directionY = Math.sin(bearingRad);
  const bins = new Map();

  lidarPoints.forEach((point) => {
    const deltaX = point.estimatedXM - robotXM;
    const deltaY = point.estimatedYM - robotYM;
    const alongM = deltaX * directionX + deltaY * directionY;

    if (
      alongM < settings.mapRayMinDistanceM ||
      alongM > settings.mapRayMaxDistanceM
    ) {
      return;
    }

    const crossM = Math.abs(deltaX * directionY - deltaY * directionX);
    const allowedCrossM =
      settings.mapRayHalfWidthM + alongM * settings.mapRayWidthGrowthPerMeter;

    if (crossM > allowedCrossM) return;

    const binIndex = Math.floor(alongM / settings.mapRayBinSizeM);
    const previous = bins.get(binIndex) ?? [];
    previous.push({ alongM, crossM, point });
    bins.set(binIndex, previous);
  });

  const supportedBins = Array.from(bins.entries())
    .map(([binIndex, points]) => ({
      binIndex: Number(binIndex),
      points,
      count: points.length,
      meanCrossM:
        points.reduce((sum, point) => sum + point.crossM, 0) / points.length,
      medianAlongM: median(points.map((point) => point.alongM)),
    }))
    .filter((bin) => bin.count >= settings.mapRayMinimumPoints);

  if (!supportedBins.length) return null;

  // Prefer a dense, narrow local surface. A small near-field penalty avoids
  // projecting markers onto very close robot/scan artifacts.
  supportedBins.sort((a, b) => {
    const scoreA =
      a.count * 2 -
      a.meanCrossM * 5 -
      Math.max(0, settings.mapRayMinDistanceM + 0.2 - a.medianAlongM) * 6;
    const scoreB =
      b.count * 2 -
      b.meanCrossM * 5 -
      Math.max(0, settings.mapRayMinDistanceM + 0.2 - b.medianAlongM) * 6;

    if (scoreA !== scoreB) return scoreB - scoreA;
    return a.medianAlongM - b.medianAlongM;
  });

  const selected = supportedBins[0];

  return {
    distanceM: selected.medianAlongM,
    supportCount: selected.count,
    meanCrossM: selected.meanCrossM,
    source: "final-lidar-map-surface",
  };
}

function average(values) {
  const valid = values.filter((value) => Number.isFinite(value));
  if (!valid.length) return null;
  return valid.reduce((sum, value) => sum + value, 0) / valid.length;
}

function median(values) {
  const valid = values
    .filter((value) => Number.isFinite(value))
    .slice()
    .sort((a, b) => a - b);

  if (!valid.length) return null;
  const middle = Math.floor(valid.length / 2);

  return valid.length % 2
    ? valid[middle]
    : (valid[middle - 1] + valid[middle]) / 2;
}

function observationWeight(observation) {
  const confidence = finite(observation?.confidence);
  return Math.max(confidence ?? 0.3, 0.1);
}

function labelForLandmark(observations) {
  const counts = new Map();

  observations.forEach((observation) => {
    const label = observation.label ?? "unknown";
    counts.set(label, (counts.get(label) ?? 0) + 1);
  });

  return (
    Array.from(counts.entries()).sort((a, b) => b[1] - a[1])[0]?.[0] ??
    "unknown"
  );
}

function pickBestObservation(observations) {
  return observations
    .slice()
    .sort((a, b) => {
      const stableDifference = Number(b.trackStableBest) - Number(a.trackStableBest);
      if (stableDifference !== 0) return stableDifference;

      const confidenceDifference = (b.confidence ?? 0) - (a.confidence ?? 0);
      if (confidenceDifference !== 0) return confidenceDifference;

      return (b.trackHits ?? 0) - (a.trackHits ?? 0);
    })[0] ?? null;
}

function trackKey(observation) {
  const trackId = finite(observation?.trackId);
  return trackId != null && trackId >= 0 ? String(trackId) : null;
}

function makeMapMeasurements(
  detectionEvents,
  poses,
  timeline,
  lidarPoints,
  settings,
) {
  if (!Array.isArray(detectionEvents)) return [];

  const measurements = [];

  detectionEvents.forEach((event) => {
    const accepted = Array.isArray(event?.acceptedDetections)
      ? event.acceptedDetections
      : [];

    if (!accepted.length || !Number.isFinite(event?.timestampMs)) return;

    const pose = findNearestEstimatedPose(poses, event.timestampMs);
    if (!pose) return;

    const environmentRow = findNearestTimelineRow(timeline, event.timestampMs);

    accepted.forEach((detection) => {
      const localBearingRad = cameraBearingRadians(event, detection, settings);
      if (localBearingRad == null) return;

      const globalBearingRad = normalizeRadians(pose.yawRad + localBearingRad);
      const mapSurface = distanceToMapSurface(
        {
          robotXM: pose.xM,
          robotYM: pose.yM,
          bearingRad: globalBearingRad,
        },
        lidarPoints,
        settings,
      );

      const boxCenter = normalizedBoxCenter(event, detection);
      const digitalZoom = Math.max(finite(event?.camera?.digitalZoom) ?? 1, 1);
      const effectiveFovDeg = effectiveHorizontalFovDeg(event, settings);
      const apparentSizeRatio = correctedApparentSizeRatio(event, detection);
      const maxReasonableDistanceM = maximumReasonableProjectionDistanceM(
        event,
        detection,
        settings,
      );
      const projectionQuality = classifyProjectionQuality({
        mapSurface,
        event,
        detection,
        settings,
      });
      const supportTooWeakForZoom =
        digitalZoom > 1.05 &&
        (mapSurface?.supportCount ?? 0) < settings.zoomedFrameMinimumSupportCount;
      const distanceInconsistent =
        mapSurface?.distanceM != null &&
        mapSurface.distanceM > maxReasonableDistanceM + settings.zoomDistanceToleranceM;
      const mapSupported =
        mapSurface?.distanceM != null &&
        !supportTooWeakForZoom &&
        !distanceInconsistent &&
        projectionQuality.level !== "low";

      const projectionDistanceM = mapSupported ? mapSurface?.distanceM ?? null : null;
      const projectedXM =
        projectionDistanceM == null
          ? null
          : pose.xM + projectionDistanceM * Math.cos(globalBearingRad);
      const projectedYM =
        projectionDistanceM == null
          ? null
          : pose.yM + projectionDistanceM * Math.sin(globalBearingRad);

      measurements.push({
        id: `${event.id}-${detection.id}`,
        eventId: event.id,
        timestampMs: event.timestampMs,
        timestampLocal: event.timestampLocal ?? null,
        imagePath: event.imagePath ?? null,
        annotatedImagePath: event.annotatedImagePath ?? event.imagePath ?? null,
        rawImagePath: event.rawImagePath ?? null,

        label: detection.label ?? "unknown",
        classId: finite(detection.classId),
        confidence: finite(detection.confidence),
        maturityScore: finite(detection.maturityScore),
        trackId: finite(detection.trackId),
        trackHits: finite(detection.trackHits),
        trackStableBest: detection.trackStableBest === true,
        bbox: detection.bbox ?? null,
        boxCenterX: boxCenter.x,
        boxCenterY: boxCenter.y,
        metrics: detection.metrics ?? {},

        robotXM: pose.xM,
        robotYM: pose.yM,
        robotYawDeg: pose.yawDeg,
        localCameraBearingDeg: radToDeg(localBearingRad),
        globalBearingDeg: radToDeg(globalBearingRad),

        digitalZoom,
        effectiveFovDeg,
        apparentSizeRatio,
        maxReasonableDistanceM,
        mapSupported,
        projectedXM,
        projectedYM,
        projectionDistanceM,
        projectionSource: mapSupported
          ? mapSurface?.source ?? "final-lidar-map-surface"
          : distanceInconsistent
            ? "zoom-distance-rejected"
            : supportTooWeakForZoom
              ? "zoom-support-rejected"
              : mapSurface?.source ?? "no-final-map-support",
        projectionQuality: projectionQuality.level,
        projectionQualityScore: projectionQuality.score,
        distanceInconsistent,
        supportTooWeakForZoom,
        mapSurface,

        environment: environmentRow
          ? {
              temperatureC: finite(environmentRow.temperatureC),
              humidityPct: finite(environmentRow.humidityPct),
              pressureHpa: finite(environmentRow.pressureHpa),
              frontDistanceM: finite(environmentRow.frontDistanceM),
              obstacleClose: environmentRow.obstacleClose ?? null,
              robotStatus: environmentRow.robotStatus ?? null,
            }
          : null,
      });
    });
  });

  return measurements.sort((a, b) => a.timestampMs - b.timestampMs);
}

function distanceBetweenMeasurementAndLandmark(measurement, landmark) {
  return Math.hypot(
    measurement.projectedXM - landmark.xM,
    measurement.projectedYM - landmark.yM,
  );
}

function createLandmark(measurement, index) {
  const weight = observationWeight(measurement);

  return {
    id: `tomato-landmark-${String(index + 1).padStart(3, "0")}`,
    xM: measurement.projectedXM,
    yM: measurement.projectedYM,
    weightSum: weight,
    observations: [measurement],
    sourceTrackIds: trackKey(measurement) ? [Number(trackKey(measurement))] : [],
    firstTimestampMs: measurement.timestampMs,
    firstTimestampLocal: measurement.timestampLocal,
    latestTimestampMs: measurement.timestampMs,
    latestTimestampLocal: measurement.timestampLocal,
    newEvidenceCount: 1,
    updateCount: 0,
  };
}

function appendMeasurementToLandmark(landmark, measurement) {
  const weight = observationWeight(measurement);
  const nextWeightSum = landmark.weightSum + weight;

  // Conservative fusion keeps an already-established map anchor stable.
  const updateWeight = Math.min(weight / nextWeightSum, 0.22);
  landmark.xM += (measurement.projectedXM - landmark.xM) * updateWeight;
  landmark.yM += (measurement.projectedYM - landmark.yM) * updateWeight;
  landmark.weightSum = nextWeightSum;
  landmark.observations.push(measurement);
  landmark.latestTimestampMs = measurement.timestampMs;
  landmark.latestTimestampLocal = measurement.timestampLocal;
  landmark.updateCount += 1;

  const key = trackKey(measurement);
  if (key && !landmark.sourceTrackIds.includes(Number(key))) {
    landmark.sourceTrackIds.push(Number(key));
  }
}

function nearestLandmarkWithinDistance(landmarks, measurement, thresholdM) {
  let best = null;

  landmarks.forEach((landmark) => {
    const distanceM = distanceBetweenMeasurementAndLandmark(measurement, landmark);

    if (distanceM <= thresholdM && (!best || distanceM < best.distanceM)) {
      best = { landmark, distanceM };
    }
  });

  return best;
}

function decorateLandmark(landmark) {
  const observations = landmark.observations;
  const representative = pickBestObservation(observations);

  return {
    ...landmark,
    observations: observations.slice(),
    representative,
    label: labelForLandmark(observations),
    classId: representative?.classId ?? null,
    count: observations.length,
    stableFrames: observations.filter((item) => item.trackStableBest).length,
    avgConfidence: average(observations.map((item) => item.confidence)),
    avgMaturityScore: average(observations.map((item) => item.maturityScore)),
    medianRangeM: median(observations.map((item) => item.projectionDistanceM)),
    mapSupportCount: observations.filter((item) => item.mapSupported).length,
    medianDigitalZoom: median(observations.map((item) => item.digitalZoom)),
    highQualityCount: observations.filter((item) => item.projectionQuality === "high").length,
    mediumQualityCount: observations.filter((item) => item.projectionQuality === "medium").length,
    latestObservation: observations.at(-1) ?? null,
  };
}

/*
 * Replays accepted measurements in chronological order. A landmark is added
 * once; later measurements only update that existing landmark when their
 * final-map projected positions are genuinely close. This gives the timeline
 * a persistent "knowledge so far" state instead of a fresh camera fan at
 * every frame.
 */
export function replayTomatoLandmarks(
  measurements,
  untilTimestampMs = Number.POSITIVE_INFINITY,
  settingsOverrides = {},
) {
  const settings = mergeSettings(settingsOverrides);
  const ordered = Array.isArray(measurements)
    ? measurements
        .filter((measurement) => Number.isFinite(measurement?.timestampMs))
        .slice()
        .sort((a, b) => a.timestampMs - b.timestampMs)
    : [];

  const landmarks = [];
  const trackBindings = new Map();
  const stats = {
    acceptedMeasurements: 0,
    mapSupportedMeasurements: 0,
    unanchoredMeasurements: 0,
    createdLandmarks: 0,
    updatedLandmarks: 0,
  };

  for (const measurement of ordered) {
    if (measurement.timestampMs > untilTimestampMs) break;

    stats.acceptedMeasurements += 1;

    if (
      measurement.mapSupported !== true ||
      !Number.isFinite(measurement.projectedXM) ||
      !Number.isFinite(measurement.projectedYM)
    ) {
      stats.unanchoredMeasurements += 1;
      continue;
    }

    stats.mapSupportedMeasurements += 1;

    const key = trackKey(measurement);
    let match = null;

    if (key && trackBindings.has(key)) {
      const candidate = trackBindings.get(key);
      const distanceM = distanceBetweenMeasurementAndLandmark(
        measurement,
        candidate,
      );

      if (distanceM <= settings.trackAssociationDistanceM) {
        match = { landmark: candidate, distanceM, via: "track" };
      }
    }

    if (!match) {
      const spatialMatch = nearestLandmarkWithinDistance(
        landmarks,
        measurement,
        settings.landmarkAssociationDistanceM,
      );

      if (spatialMatch) {
        match = { ...spatialMatch, via: "position" };
      }
    }

    if (match) {
      appendMeasurementToLandmark(match.landmark, measurement);
      stats.updatedLandmarks += 1;

      if (key) trackBindings.set(key, match.landmark);
      continue;
    }

    const landmark = createLandmark(measurement, landmarks.length);
    landmarks.push(landmark);
    stats.createdLandmarks += 1;

    if (key) trackBindings.set(key, landmark);
  }

  return {
    landmarks: landmarks.map(decorateLandmark),
    stats,
  };
}

export function buildOccupancyCells(lidarPoints, cellSizeM) {
  const cellSize = clamp(
    Number.isFinite(cellSizeM)
      ? cellSizeM
      : DEFAULT_SESSION_MAP_SETTINGS.occupancyCellSizeM,
    0.04,
    0.5,
  );

  const cells = new Map();

  lidarPoints.forEach((point) => {
    if (!Number.isFinite(point.estimatedXM) || !Number.isFinite(point.estimatedYM)) {
      return;
    }

    const gridX = Math.floor(point.estimatedXM / cellSize);
    const gridY = Math.floor(point.estimatedYM / cellSize);
    const key = `${gridX}:${gridY}`;
    const previous = cells.get(key) ?? {
      id: key,
      gridX,
      gridY,
      xM: (gridX + 0.5) * cellSize,
      yM: (gridY + 0.5) * cellSize,
      count: 0,
    };

    previous.count += 1;
    cells.set(key, previous);
  });

  const array = Array.from(cells.values());
  const maxCount = Math.max(...array.map((cell) => cell.count), 1);

  return array.map((cell) => ({
    ...cell,
    sizeM: cellSize,
    density: cell.count / maxCount,
  }));
}

export function calculateEstimatedMapBounds({
  poses = [],
  lidarPoints = [],
  tomatoLandmarks = [],
} = {}) {
  const points = [
    ...poses.map((pose) => ({ xM: pose.xM, yM: pose.yM })),
    ...lidarPoints.map((point) => ({
      xM: point.estimatedXM,
      yM: point.estimatedYM,
    })),
    ...tomatoLandmarks.map((landmark) => ({
      xM: landmark.xM,
      yM: landmark.yM,
    })),
  ].filter((point) => Number.isFinite(point.xM) && Number.isFinite(point.yM));

  if (!points.length) {
    return {
      minXM: -1,
      maxXM: 1,
      minYM: -1,
      maxYM: 1,
      widthM: 2,
      heightM: 2,
    };
  }

  let minXM = points[0].xM;
  let maxXM = points[0].xM;
  let minYM = points[0].yM;
  let maxYM = points[0].yM;

  points.forEach((point) => {
    minXM = Math.min(minXM, point.xM);
    maxXM = Math.max(maxXM, point.xM);
    minYM = Math.min(minYM, point.yM);
    maxYM = Math.max(maxYM, point.yM);
  });

  const paddingM = Math.max(0.55, Math.max(maxXM - minXM, maxYM - minYM) * 0.08);

  return {
    minXM: minXM - paddingM,
    maxXM: maxXM + paddingM,
    minYM: minYM - paddingM,
    maxYM: maxYM + paddingM,
    widthM: Math.max(maxXM - minXM + paddingM * 2, 0.01),
    heightM: Math.max(maxYM - minYM + paddingM * 2, 0.01),
  };
}

export function maturityDisplayColor(rawScore, label = "") {
  const normalizedLabel = String(label).toLowerCase();

  if (normalizedLabel.includes("unripe")) return "#22c55e";
  if (normalizedLabel.includes("ripe")) return "#ef4444";

  const score = clamp(Number.isFinite(rawScore) ? rawScore : 0.5, 0, 1);
  if (score < 0.25) return "#16a34a";
  if (score < 0.5) return "#84cc16";
  if (score < 0.75) return "#f59e0b";
  return "#ef4444";
}

export function buildEstimatedSessionMap(
  { timeline = [], lidarPreview = null, detectionEvents = [] } = {},
  settingsOverrides = {},
) {
  const settings = mergeSettings(settingsOverrides);
  const poses = estimateSessionPoses(timeline, settings);
  const lidarPoints = transformLidarPreviewPoints(lidarPreview, poses);
  const measurements = makeMapMeasurements(
    detectionEvents,
    poses,
    timeline,
    lidarPoints,
    settings,
  );
  const finalReplay = replayTomatoLandmarks(measurements, Number.POSITIVE_INFINITY, settings);
  const occupancyCells = buildOccupancyCells(lidarPoints, settings.occupancyCellSizeM);
  const bounds = calculateEstimatedMapBounds({
    poses,
    lidarPoints,
    tomatoLandmarks: finalReplay.landmarks,
  });

  return {
    settings,
    poses,
    lidarPoints,
    occupancyCells,
    measurements,
    finalLandmarks: finalReplay.landmarks,
    finalStats: finalReplay.stats,
    bounds,
  };
}
