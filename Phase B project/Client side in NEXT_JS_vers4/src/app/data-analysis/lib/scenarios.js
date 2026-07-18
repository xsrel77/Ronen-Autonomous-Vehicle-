export const SCENARIOS = {
  baseline: {
    id: "baseline",
    label: "Baseline",
    description: "Normal prototype state using the current tomato maturity data.",
  },
  stress: {
    id: "stress",
    label: "Stress / disturbance",
    description: "Simulates greenhouse stress by reducing tomato maturity and confidence, mainly in the middle row zone.",
  },
  recovery: {
    id: "recovery",
    label: "Recovery",
    description: "Simulates recovery after stress by improving maturity and confidence in the affected zone.",
  },
};

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

function colorFromScore(score) {
  if (score >= 0.8) return "#dc2626";
  if (score >= 0.58) return "#f97316";
  if (score >= 0.32) return "#f59e0b";
  if (score >= 0.12) return "#22c55e";
  return "#15803d";
}

export function applyScenarioToSamples(samples, scenarioId) {
  return samples.map((sample) => {
    const affectedZone = sample.y >= 7 && sample.y <= 18;

    let score = sample.maturityScore ?? sample.continuousMaturityScore ?? 0.5;
    let confidence = sample.confidence ?? 0.8;

    if (scenarioId === "stress") {
      score = affectedZone
        ? clamp(score - 0.45, 0, 1)
        : clamp(score - 0.18, 0, 1);

      confidence = affectedZone
        ? clamp(confidence - 0.3, 0.35, 1)
        : clamp(confidence - 0.12, 0.45, 1);
    }

    if (scenarioId === "recovery") {
      score = affectedZone
        ? clamp(score + 0.28, 0, 1)
        : clamp(score + 0.08, 0, 1);

      confidence = affectedZone
        ? clamp(confidence + 0.14, 0, 1)
        : clamp(confidence + 0.06, 0, 1);
    }

    const classId = classFromScore(score);

    return {
      ...sample,
      maturityScore: score,
      continuousMaturityScore: score,
      confidence,
      classId,
      color: colorFromScore(score),
      label: `${SCENARIOS[scenarioId]?.label ?? "Baseline"} · ${sample.label ?? "Tomato cluster"}`,
      scenario: scenarioId,
    };
  });
}