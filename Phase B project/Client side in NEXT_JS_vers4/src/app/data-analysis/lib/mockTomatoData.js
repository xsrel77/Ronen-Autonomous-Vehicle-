import c0v1 from "../mock-greenhouse-images/class-0-fully-ripe-bunch-01.png";
import c0v2 from "../mock-greenhouse-images/class-0-fully-ripe-bunch-02.png";
import c0v3 from "../mock-greenhouse-images/class-0-fully-ripe-bunch-03.png";
import c1v1 from "../mock-greenhouse-images/class-1-ripe-tomato-01.png";
import c1v2 from "../mock-greenhouse-images/class-1-ripe-tomato-02.png";
import c1v3 from "../mock-greenhouse-images/class-1-ripe-tomato-03.png";
import c2v1 from "../mock-greenhouse-images/class-2-turning-mixed-color-01.png";
import c2v2 from "../mock-greenhouse-images/class-2-turning-mixed-color-02.png";
import c2v3 from "../mock-greenhouse-images/class-2-turning-mixed-color-03.png";
import c3v1 from "../mock-greenhouse-images/class-3-green-tomato-01.png";
import c3v2 from "../mock-greenhouse-images/class-3-green-tomato-02.png";
import c3v3 from "../mock-greenhouse-images/class-3-green-tomato-03.png";
import c4v1 from "../mock-greenhouse-images/class-4-mixed-bunch-01.png";
import c4v2 from "../mock-greenhouse-images/class-4-mixed-bunch-02.png";
import c4v3 from "../mock-greenhouse-images/class-4-mixed-bunch-03.png";
import c5v1 from "../mock-greenhouse-images/class-5-unripe-bunch-01.png";
import c5v2 from "../mock-greenhouse-images/class-5-unripe-bunch-02.png";
import c5v3 from "../mock-greenhouse-images/class-5-unripe-bunch-03.png";

export const TOMATO_CLASSES = {
  0: { label: "Fully ripe bunch", hebrew: "אשכול בשל לחלוטין", score: 1.0, color: "#7f1d1d", slug: "fully-ripe-bunch" },
  1: { label: "Ripe tomato", hebrew: "עגבנייה בשלה", score: 0.9, color: "#dc2626", slug: "ripe-tomato" },
  2: { label: "Turning / mixed color", hebrew: "עגבנייה במעבר", score: 0.5, color: "#f59e0b", slug: "turning-mixed-color" },
  3: { label: "Green tomato", hebrew: "עגבנייה ירוקה", score: 0.1, color: "#22c55e", slug: "green-tomato" },
  4: { label: "Mixed bunch", hebrew: "אשכול מעורב", score: 0.6, color: "#f97316", slug: "mixed-bunch" },
  5: { label: "Unripe bunch", hebrew: "אשכול ירוק", score: 0.0, color: "#15803d", slug: "unripe-bunch" },
};

const TOMATO_IMAGES = {
  0: [c0v1, c0v2, c0v3],
  1: [c1v1, c1v2, c1v3],
  2: [c2v1, c2v2, c2v3],
  3: [c3v1, c3v2, c3v3],
  4: [c4v1, c4v2, c4v3],
  5: [c5v1, c5v2, c5v3],
};

export const TIME_SCALE_OPTIONS = {
  seconds: { label: "Seconds", unit: "s", stepMs: 1000 },
  minutes: { label: "Minutes", unit: "min", stepMs: 60_000 },
  hours: { label: "Hours", unit: "h", stepMs: 3_600_000 },
  days: { label: "Days", unit: "d", stepMs: 86_400_000 },
};

const TIME_SCALE_MS = {
  seconds: TIME_SCALE_OPTIONS.seconds.stepMs,
  minutes: TIME_SCALE_OPTIONS.minutes.stepMs,
  hours: TIME_SCALE_OPTIONS.hours.stepMs,
  days: TIME_SCALE_OPTIONS.days.stepMs,
};

function scaleLabelShort(scale) {
  if (scale === "seconds") return "sec";
  if (scale === "minutes") return "min";
  if (scale === "hours") return "hour";
  return "day";
}

export const GREENHOUSE_LAYOUT = {
  widthM: 14,
  heightM: 24,
  rows: [
    { id: "R1", x: 7.0, y1: 1.2, y2: 22.8, width: 1.15 },
  ],
  aisles: [
    { id: "A1", x: 9.2, label: "Aisle 1", adjacentRows: ["R1"] },
  ],
};

function seededRandom(seed) {
  let value = seed % 2147483647;
  if (value <= 0) value += 2147483646;

  return () => {
    value = (value * 16807) % 2147483647;
    return (value - 1) / 2147483646;
  };
}

function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}

function classFromScore(score) {
  if (score >= 0.95) return 0;
  if (score >= 0.8) return 1;
  if (score >= 0.58) return 4;
  if (score >= 0.32) return 2;
  if (score >= 0.12) return 3;
  return 5;
}

export function getMockTomatoImage(classId, variant = 1) {
  const images = TOMATO_IMAGES[classId] ?? TOMATO_IMAGES[3];
  const safeIndex = clamp(Math.round(variant), 1, images.length) - 1;
  return images[safeIndex];
}

/*
 * Stable physical mock plants. Their IDs and positions do not change across
 * scans, so accumulated detections form one aligned top-to-bottom tomato row.
 */
const MOCK_TOMATO_ANCHORS = Array.from({ length: 14 }, (_, index) => {
  const y = 2.0 + index * 1.5;

  return {
    id: `T-${String(index + 1).padStart(2, "0")}`,
    row: "R1",
    x: Number((7.0 + (index % 2 === 0 ? -0.08 : 0.08)).toFixed(2)),
    y: Number(y.toFixed(2)),
    index,
    imageVariant: (index % 3) + 1,
    bunchLike: index % 4 === 0 || index % 4 === 3,
    baseRipeness: Number((0.03 + index * 0.045).toFixed(3)),
  };
});

function createMockTimeline() {
  const random = seededRandom(4217);
  const timestamps = [];

  for (let i = 0; i < 30; i += 1) timestamps.push(Math.round(i * 42_000 + random() * 12_000));
  for (let i = 0; i < 24; i += 1) timestamps.push(30 * 60_000 + Math.round(i * 75_000 + random() * 18_000));
  for (let i = 0; i < 22; i += 1) timestamps.push(2 * 3_600_000 + Math.round(i * 180_000 + random() * 35_000));
  for (let i = 0; i < 2; i += 1) timestamps.push(5 * 3_600_000 + Math.round(i * 12 * 60_000 + random() * 60_000));
  for (let i = 0; i < 22; i += 1) timestamps.push(6 * 3_600_000 + Math.round(i * 90_000 + random() * 22_000));

  timestamps.sort((a, b) => a - b);
  const frames = [];
  const sweepSpan = 20.2;
  const sweepLength = 25;

  for (let i = 0; i < timestamps.length; i += 1) {
    const sweepIndex = Math.floor(i / sweepLength);
    const sweepProgress = ((i % sweepLength) + random() * 0.32) / (sweepLength - 1);
    const movingDown = sweepIndex % 2 === 0;
    const y = movingDown
      ? 1.8 + sweepProgress * sweepSpan
      : 22.0 - sweepProgress * sweepSpan;

    const robotPose = {
      x: 9.2,
      y: Number(clamp(y, 1.7, 22.3).toFixed(2)),
      yawDeg: movingDown ? 180 : 0,
      aisle: "A1",
    };

    const nearby = MOCK_TOMATO_ANCHORS
      .filter((anchor) => Math.abs(anchor.y - robotPose.y) <= 3.3)
      .sort((a, b) => Math.abs(a.y - robotPose.y) - Math.abs(b.y - robotPose.y));

    const detectionsPerFrame = 1 + (i % 3);

    const detections = nearby.slice(0, detectionsPerFrame).map((anchor) => {
      const timeProgress = timestamps[i] / timestamps[timestamps.length - 1];
      const verticalProgress = anchor.y / GREENHOUSE_LAYOUT.heightM;
      const noise = (random() - 0.5) * 0.1;
      const score = clamp(
        anchor.baseRipeness + timeProgress * 0.42 + verticalProgress * 0.18 + noise,
        0,
        1,
      );

      const classId = classFromScore(score);
      const cls = TOMATO_CLASSES[classId];

      return {
        id: anchor.id,
        classId,
        confidence: Number(clamp(0.74 + random() * 0.22, 0.74, 0.96).toFixed(3)),
        count: classId === 0 || classId === 4 || classId === 5 || anchor.bunchLike
          ? 4 + (anchor.index % 5)
          : 1,
        x: anchor.x,
        y: anchor.y,
        row: anchor.row,
        label: cls.label,
        hebrewLabel: cls.hebrew,
        maturityScore: cls.score,
        continuousMaturityScore: Number(score.toFixed(3)),
        color: cls.color,
        image: getMockTomatoImage(classId, anchor.imageVariant),
        source: "mock-yolo12m",
      };
    });

    frames.push({
      sampleId: i,
      timestampMs: timestamps[i],
      robotPose,
      detections,
    });
  }

  return frames;
}

export const MOCK_YOLO_TIMELINE = createMockTimeline();
const MOCK_START_MS = MOCK_YOLO_TIMELINE[0]?.timestampMs ?? 0;

export function formatTimelineTime(timestampMs, scale = "minutes") {
  const option = TIME_SCALE_OPTIONS[scale] ?? TIME_SCALE_OPTIONS.minutes;
  const value = timestampMs / option.stepMs;
  if (scale === "seconds") return `${Math.round(value)}s`;
  if (scale === "minutes") return `${Math.round(value)}m`;
  if (scale === "hours") return `hour ${Math.floor(value)}`;
  return `day ${Math.floor(value)}`;
}

export function formatBucketLabel(bucket, scale = "minutes") {
  if (!bucket) return "—";
  if (scale === "seconds") return `${bucket.bucketKey}s`;
  if (scale === "minutes") return `min ${bucket.bucketKey}`;
  if (scale === "hours") return `hour ${bucket.bucketKey}`;
  return `day ${bucket.bucketKey}`;
}

export function enrichDetection(detection, frame) {
  const cls = TOMATO_CLASSES[detection.classId] ?? TOMATO_CLASSES[3];

  return {
    ...detection,
    sampleId: frame.sampleId,
    timestampMs: frame.timestampMs,
    scanPose: frame.robotPose,
    label: detection.label ?? cls.label,
    hebrewLabel: detection.hebrewLabel ?? cls.hebrew,
    maturityScore: detection.continuousMaturityScore ?? cls.score,
    displayScore: cls.score,
    color: cls.color,
    image: detection.image ?? getMockTomatoImage(detection.classId, 1),
    source: detection.source ?? "mock-yolo12m",
  };
}

export function getTimelineBuckets(scale = "minutes") {
  const step = TIME_SCALE_MS[scale] ?? TIME_SCALE_MS.minutes;

  const frames = MOCK_YOLO_TIMELINE.map((frame) => ({
    ...frame,
    bucketIndex: Math.floor((frame.timestampMs - MOCK_START_MS) / step),
  }));

  const maxFrameIndex = Math.max(...frames.map((frame) => frame.bucketIndex), 0);
  const buckets = [];
  const latestByCluster = new Map();

  for (let bucketIndex = 0; bucketIndex <= maxFrameIndex; bucketIndex += 1) {
    const startMs = MOCK_START_MS + bucketIndex * step;
    const endMs = startMs + step;
    const updates = frames.filter((frame) => frame.bucketIndex === bucketIndex);

    updates.forEach((frame) => {
      frame.detections.forEach((detection) => {
        latestByCluster.set(detection.id, detection);
      });
    });

    const accumulated = Array.from(latestByCluster.values());
    const totalKnown = accumulated.length;
    const avgMaturity = totalKnown
      ? accumulated.reduce((sum, item) => sum + (item.continuousMaturityScore ?? item.maturityScore ?? 0), 0) / totalKnown
      : 0;

    const ripe = accumulated.filter((item) => (item.continuousMaturityScore ?? item.maturityScore ?? 0) >= 0.75).length;
    const turning = accumulated.filter((item) => {
      const score = item.continuousMaturityScore ?? item.maturityScore ?? 0;
      return score >= 0.42 && score < 0.75;
    }).length;
    const green = accumulated.filter((item) => (item.continuousMaturityScore ?? item.maturityScore ?? 0) < 0.42).length;

    buckets.push({
      id: `${scale}-${bucketIndex}`,
      label: `${scaleLabelShort(scale)} ${bucketIndex + 1}`,
      bucketIndex,
      startMs,
      endMs,
      updateCount: updates.length,
      frameCount: updates.length,
      latestFrame: updates[updates.length - 1] ?? null,
      totalKnownDetections: totalKnown,
      avgMaturityPercent: Math.round(avgMaturity * 100),
      ripeCount: ripe,
      turningCount: turning,
      greenCount: green,
      detections: accumulated,
      maturityGroups: { ripe, turning, green, total: totalKnown },
    });
  }

  return buckets;
}

export function getAccumulatedDetectionsUpToBucket(bucketPosition, scale = "minutes") {
  const buckets = getTimelineBuckets(scale);
  const active = buckets[clamp(bucketPosition, 0, buckets.length - 1)] ?? buckets[0];
  if (!active) return [];

  const byCluster = new Map();

  MOCK_YOLO_TIMELINE
    .filter((frame) => frame.timestampMs < active.endMs)
    .forEach((frame) => {
      frame.detections.forEach((detection) => {
        byCluster.set(detection.id, enrichDetection(detection, frame));
      });
    });

  return Array.from(byCluster.values()).sort((a, b) => a.y - b.y);
}

export function getDetectionsInBucket(bucketPosition, scale = "minutes") {
  const buckets = getTimelineBuckets(scale);
  const active = buckets[clamp(bucketPosition, 0, buckets.length - 1)];
  if (!active) return [];

  const currentFrame = active.latestFrame;
  return currentFrame
    ? currentFrame.detections.map((detection) => enrichDetection(detection, currentFrame))
    : [];
}

export function getCurrentRobotPoseForBucket(bucketPosition, scale = "minutes") {
  const buckets = getTimelineBuckets(scale);
  return (
    buckets[clamp(bucketPosition, 0, buckets.length - 1)]?.latestFrame?.robotPose ??
    MOCK_YOLO_TIMELINE[0].robotPose
  );
}

// Legacy helpers kept so existing imports do not break.
export function getTimelineUpTo(sampleId) {
  return MOCK_YOLO_TIMELINE
    .filter((frame) => frame.sampleId <= sampleId)
    .flatMap((frame) => frame.detections.map((detection) => enrichDetection(detection, frame)));
}

export function getRobotPathUpTo(sampleId) {
  return MOCK_YOLO_TIMELINE
    .filter((frame) => frame.sampleId <= sampleId)
    .map((frame) => ({
      sampleId: frame.sampleId,
      timestampMs: frame.timestampMs,
      ...frame.robotPose,
    }));
}

export function getFrameBySampleId(sampleId) {
  return MOCK_YOLO_TIMELINE.find((frame) => frame.sampleId === sampleId) ?? MOCK_YOLO_TIMELINE[0];
}
