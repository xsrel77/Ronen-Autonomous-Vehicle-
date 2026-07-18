export const TIME_SCALE_OPTIONS = {
  seconds: { label: "Seconds", stepMs: 1_000 },
  minutes: { label: "Minutes", stepMs: 60_000 },
  hours: { label: "Hours", stepMs: 3_600_000 },
  days: { label: "Days", stepMs: 86_400_000 },
};

function finite(value, fallback = null) {
  if (value == null || value === "") return fallback;
  const number = Number(value);
  return Number.isFinite(number) ? number : fallback;
}

function formatElapsed(milliseconds, scale) {
  const elapsedMs = Math.max(0, milliseconds);

  if (scale === "seconds") return `${Math.floor(elapsedMs / 1_000)}s`;
  if (scale === "minutes") return `min ${Math.floor(elapsedMs / 60_000)}`;
  if (scale === "hours") return `hour ${Math.floor(elapsedMs / 3_600_000)}`;
  return `day ${Math.floor(elapsedMs / 86_400_000)}`;
}

function maturityGroups(samples) {
  const groups = { green: 0, turning: 0, ripe: 0, total: samples.length };

  samples.forEach((sample) => {
    const value = finite(sample?.maturityScore, 0.5);
    if (value <= 0.25) groups.green += 1;
    else if (value >= 0.75) groups.ripe += 1;
    else groups.turning += 1;
  });

  return groups;
}

export function buildSessionTimelineBuckets({ landmarks = [], observations = [], robotTrail = [] } = {}, timeScale = "seconds") {
  const stepMs = TIME_SCALE_OPTIONS[timeScale]?.stepMs ?? TIME_SCALE_OPTIONS.seconds.stepMs;
  const timestamps = [
    ...landmarks.map((item) => finite(item?.firstTimestampMs)),
    ...observations.map((item) => finite(item?.timestampMs)),
    ...robotTrail.map((item) => finite(item?.timestampMs)),
  ].filter((value) => value != null);

  if (!timestamps.length) return [];

  const startTimestampMs = Math.min(...timestamps);
  const endTimestampMs = Math.max(...timestamps);
  const bucketCount = Math.max(1, Math.floor((endTimestampMs - startTimestampMs) / stepMs) + 1);

  return Array.from({ length: bucketCount }, (_, index) => {
    const startMs = startTimestampMs + index * stepMs;
    const endMs = Math.min(endTimestampMs, startMs + stepMs - 1);
    const knownLandmarks = landmarks.filter(
      (landmark) => finite(landmark?.firstTimestampMs, Infinity) <= endMs,
    );
    const updates = observations.filter((observation) => {
      const timestampMs = finite(observation?.timestampMs, -Infinity);
      return timestampMs >= startMs && timestampMs <= endMs;
    });
    const avgMaturity = knownLandmarks.length
      ? knownLandmarks.reduce((sum, item) => sum + finite(item?.maturityScore, 0.5), 0) /
        knownLandmarks.length
      : 0;

    return {
      id: `${timeScale}-${index}`,
      index,
      startTimestampMs: startMs,
      endTimestampMs: endMs,
      label: formatElapsed(startMs - startTimestampMs, timeScale),
      updateCount: updates.length,
      updatedLandmarkIds: [...new Set(updates.map((item) => item?.landmarkId).filter(Boolean))],
      totalKnownDetections: knownLandmarks.length,
      avgMaturityPercent: Math.round(avgMaturity * 100),
      maturityGroups: maturityGroups(knownLandmarks),
    };
  });
}

export function getRobotPoseAtOrBefore(robotTrail = [], timestampMs) {
  const rows = Array.isArray(robotTrail) ? robotTrail : [];
  if (!rows.length) return null;
  if (!Number.isFinite(timestampMs)) return rows.at(-1) ?? null;

  let selected = rows[0];

  for (const row of rows) {
    if (finite(row?.timestampMs, -Infinity) <= timestampMs) selected = row;
    else break;
  }

  return selected;
}
