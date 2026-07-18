function clamp(value, minimum, maximum) {
  return Math.max(minimum, Math.min(maximum, value));
}

function mean(values) {
  const usable = values.filter((value) => Number.isFinite(value));
  if (!usable.length) return 0;
  return usable.reduce((sum, value) => sum + value, 0) / usable.length;
}

function weightedMean(values) {
  let weightedSum = 0;
  let weightSum = 0;

  values.forEach(({ value, weight }) => {
    if (!Number.isFinite(value) || !Number.isFinite(weight) || weight <= 0) return;
    weightedSum += value * weight;
    weightSum += weight;
  });

  return weightSum > 0 ? weightedSum / weightSum : 0;
}

function median(values) {
  return quantile(values, 0.5);
}

function quantile(values, ratio) {
  const usable = values
    .filter((value) => Number.isFinite(value))
    .slice()
    .sort((a, b) => a - b);

  if (!usable.length) return null;
  const bounded = clamp(ratio, 0, 1);
  const index = (usable.length - 1) * bounded;
  const lower = Math.floor(index);
  const upper = Math.ceil(index);

  if (lower === upper) return usable[lower];
  const blend = index - lower;
  return usable[lower] + (usable[upper] - usable[lower]) * blend;
}

function distance(a, b) {
  return Math.hypot(a.x - b.x, a.y - b.y);
}

function usableSamples(samples = []) {
  return samples.filter(
    (sample) =>
      Number.isFinite(sample?.x) &&
      Number.isFinite(sample?.y) &&
      Number.isFinite(sample?.maturityScore),
  );
}

function sampleEvidenceCount(sample) {
  const count = Number(sample?.sourceObservationCount ?? sample?.observationCount ?? 1);
  return clamp(Number.isFinite(count) ? count : 1, 1, 24);
}

function sampleEvidenceWeight(sample) {
  const confidence = clamp(Number(sample?.confidence ?? 0.7), 0.1, 1);
  return confidence * Math.sqrt(sampleEvidenceCount(sample));
}

function nearestNeighborDistances(points) {
  if (!Array.isArray(points) || points.length < 2) return [];

  return points.map((point, index) =>
    points.reduce((closest, candidate, candidateIndex) => {
      if (candidateIndex === index) return closest;
      return Math.min(closest, distance(point, candidate));
    }, Number.POSITIVE_INFINITY),
  ).filter(Number.isFinite);
}

/*
 * Every visible strong tracker remains an independent UI marker. For the
 * numerical model only, nearby approximate map projections are combined into
 * local support anchors. Each original marker still contributes through its
 * confidence and observation count; this avoids an ill-conditioned covariance
 * matrix without discarding the available accepted data.
 */
export function buildKrigingSupportAnchors(samples = [], layout = {}) {
  const points = usableSamples(samples);
  const minimumSpan = Math.max(
    0.1,
    Math.min(Number(layout?.widthM) || 1, Number(layout?.heightM) || 1),
  );
  const cellSizeM = clamp(minimumSpan / 45, 0.08, 0.14);
  const groups = new Map();

  points.forEach((point) => {
    const key = `${Math.round(point.x / cellSizeM)}:${Math.round(point.y / cellSizeM)}`;
    const group = groups.get(key) ?? [];
    group.push(point);
    groups.set(key, group);
  });

  const anchors = Array.from(groups.entries()).map(([key, group], index) => {
    let weightSum = 0;
    let xSum = 0;
    let ySum = 0;
    let maturitySum = 0;
    let confidenceSum = 0;
    let sourceObservationCount = 0;

    group.forEach((point) => {
      const confidence = clamp(Number(point.confidence ?? 0.7), 0.1, 1);
      const weight = sampleEvidenceWeight(point);
      const observationCount = sampleEvidenceCount(point);

      weightSum += weight;
      xSum += point.x * weight;
      ySum += point.y * weight;
      maturitySum += point.maturityScore * weight;
      confidenceSum += confidence * weight;
      sourceObservationCount += observationCount;
    });

    const safeWeight = Math.max(weightSum, 1e-8);
    return {
      id: `kriging-support-${String(index + 1).padStart(3, "0")}`,
      sourceCellKey: key,
      x: xSum / safeWeight,
      y: ySum / safeWeight,
      maturityScore: maturitySum / safeWeight,
      confidence: confidenceSum / safeWeight,
      sourceAnchorCount: group.length,
      sourceObservationCount,
      evidenceWeight: safeWeight,
    };
  });

  return {
    anchors,
    cellSizeM,
    displayAnchorCount: points.length,
    displayObservationCount: points.reduce(
      (sum, point) => sum + sampleEvidenceCount(point),
      0,
    ),
  };
}

function deterministicShuffle(values, seed) {
  const output = values.slice();
  let state = seed >>> 0;

  function random() {
    state = (state * 1664525 + 1013904223) >>> 0;
    return state / 4294967296;
  }

  for (let index = output.length - 1; index > 0; index -= 1) {
    const swapIndex = Math.floor(random() * (index + 1));
    [output[index], output[swapIndex]] = [output[swapIndex], output[index]];
  }

  return output;
}

export function calculateMoranI(samples, thresholdM = 0.45) {
  const points = usableSamples(samples);
  const n = points.length;
  if (n < 3) return { value: 0, label: "Not enough spatial anchors", pValue: null };

  const average = mean(points.map((point) => point.maturityScore));
  const denominator = points.reduce(
    (sum, point) => sum + (point.maturityScore - average) ** 2,
    0,
  );

  let numerator = 0;
  let weightSum = 0;

  for (let i = 0; i < n; i += 1) {
    for (let j = 0; j < n; j += 1) {
      if (i === j) continue;
      const d = distance(points[i], points[j]);
      if (d > thresholdM) continue;
      const weight = 1 / Math.max(0.05, d);
      numerator += weight * (points[i].maturityScore - average) * (points[j].maturityScore - average);
      weightSum += weight;
    }
  }

  if (!weightSum || !denominator) {
    return { value: 0, label: "No measurable maturity variation", pValue: null };
  }

  const value = (n / weightSum) * (numerator / denominator);
  const label =
    value > 0.3
      ? "Positive spatial autocorrelation"
      : value < -0.15
        ? "Negative spatial autocorrelation"
        : "Weak spatial structure";

  return { value, label, pValue: null };
}

export function calculateMoranPermutationTest(samples, thresholdM = 0.45, permutations = 199) {
  const points = usableSamples(samples);
  if (points.length < 3) return { observed: 0, pValue: null };

  const observed = calculateMoranI(points, thresholdM).value;
  const scores = points.map((point) => point.maturityScore);
  let extremeCount = 0;

  for (let iteration = 0; iteration < permutations; iteration += 1) {
    const shuffled = deterministicShuffle(scores, 9127 + iteration * 97);
    const simulated = calculateMoranI(
      points.map((point, index) => ({ ...point, maturityScore: shuffled[index] })),
      thresholdM,
    ).value;

    if (Math.abs(simulated) >= Math.abs(observed)) extremeCount += 1;
  }

  return {
    observed,
    pValue: (extremeCount + 1) / (permutations + 1),
  };
}

export function calculateGearyC(samples, thresholdM = 0.45) {
  const points = usableSamples(samples);
  const n = points.length;
  if (n < 3) return { value: 1, label: "Not enough spatial anchors" };

  const average = mean(points.map((point) => point.maturityScore));
  const denominator = points.reduce(
    (sum, point) => sum + (point.maturityScore - average) ** 2,
    0,
  );

  let numerator = 0;
  let weightSum = 0;

  for (let i = 0; i < n; i += 1) {
    for (let j = i + 1; j < n; j += 1) {
      const d = distance(points[i], points[j]);
      if (d > thresholdM) continue;
      const weight = 1 / Math.max(0.05, d);
      numerator += weight * (points[i].maturityScore - points[j].maturityScore) ** 2;
      weightSum += weight;
    }
  }

  if (!weightSum || !denominator) return { value: 1, label: "No measurable maturity variation" };

  const value = ((n - 1) * numerator) / (2 * weightSum * denominator);
  return {
    value,
    label: value < 1 ? "Nearby anchors are similar" : "Nearby anchors are dissimilar",
  };
}

export function estimateVariogram(samples, layout = {}) {
  const points = usableSamples(samples);
  const minimumSpan = Math.max(
    0.1,
    Math.min(Number(layout?.widthM) || 1, Number(layout?.heightM) || 1),
  );

  if (points.length < 2) {
    return {
      model: "Spherical",
      nugget: 0.03,
      sill: 0.08,
      rangeMeters: clamp(minimumSpan * 0.3, 0.75, 2.5),
      pairs: [],
    };
  }

  const pairs = [];
  for (let i = 0; i < points.length; i += 1) {
    for (let j = i + 1; j < points.length; j += 1) {
      const d = distance(points[i], points[j]);
      if (d <= 0) continue;
      pairs.push({
        distance: d,
        semivariance: 0.5 * (points[i].maturityScore - points[j].maturityScore) ** 2,
        weight: Math.sqrt(sampleEvidenceCount(points[i]) * sampleEvidenceCount(points[j])),
      });
    }
  }

  const distances = pairs.map((pair) => pair.distance);
  const semivariances = pairs.map((pair) => pair.semivariance);
  const averageMaturity = mean(points.map((point) => point.maturityScore));
  const sampleVariance = weightedMean(
    points.map((point) => ({
      value: (point.maturityScore - averageMaturity) ** 2,
      weight: sampleEvidenceWeight(point),
    })),
  );
  const empiricalRange = quantile(distances, 0.65) ?? minimumSpan * 0.3;
  const neighbourDistance = quantile(nearestNeighborDistances(points), 0.75) ?? 0;
  // The legacy ecological model used a broad empirical 65th-percentile range.
  // The lower bound below carries that behaviour into the smaller ROS2 map
  // extent, rather than shrinking the field to a few camera-projection pixels.
  const rangeMeters = clamp(
    Math.max(empiricalRange, neighbourDistance * 6, minimumSpan * 0.3),
    0.75,
    Math.max(0.9, minimumSpan * 0.55),
  );
  const sill = clamp(
    Math.max(
      sampleVariance,
      weightedMean(pairs.map((pair) => ({ value: pair.semivariance, weight: pair.weight }))) * 1.5,
      0.05,
    ),
    0.05,
    1,
  );
  const nugget = clamp(
    Math.min(median(semivariances) ?? 0.04, sill * 0.55),
    0.03,
    0.22,
  );

  return {
    model: "Spherical",
    nugget,
    sill,
    rangeMeters,
    pairs: pairs.sort((a, b) => a.distance - b.distance),
  };
}

function sphericalVariogram(distanceM, variogram) {
  const distanceValue = Math.max(0, distanceM);
  const range = Math.max(variogram.rangeMeters, 0.001);
  const nugget = variogram.nugget;
  const sill = variogram.sill;

  if (distanceValue === 0) return 0;
  if (distanceValue >= range) return nugget + sill;

  const ratio = distanceValue / range;
  return nugget + sill * (1.5 * ratio - 0.5 * ratio ** 3);
}

function solveLinearSystem(matrix, vector) {
  const size = matrix.length;
  const augmented = matrix.map((row, index) => [...row, vector[index]]);

  for (let column = 0; column < size; column += 1) {
    let pivot = column;
    for (let row = column + 1; row < size; row += 1) {
      if (Math.abs(augmented[row][column]) > Math.abs(augmented[pivot][column])) pivot = row;
    }

    if (Math.abs(augmented[pivot][column]) < 1e-10) return null;

    [augmented[column], augmented[pivot]] = [augmented[pivot], augmented[column]];
    const divisor = augmented[column][column];

    for (let col = column; col <= size; col += 1) augmented[column][col] /= divisor;

    for (let row = 0; row < size; row += 1) {
      if (row === column) continue;
      const factor = augmented[row][column];
      if (factor === 0) continue;
      for (let col = column; col <= size; col += 1) {
        augmented[row][col] -= factor * augmented[column][col];
      }
    }
  }

  return augmented.map((row) => row[size]);
}

function inverseDistancePrediction(samples, target, variogram) {
  let weightedValue = 0;
  let weightSum = 0;
  let nearest = Infinity;

  samples.forEach((sample) => {
    const d = Math.max(0.04, distance(sample, target));
    const confidence = clamp(Number(sample.confidence ?? 0.7), 0.1, 1);
    const weight = confidence / d ** 2;
    weightedValue += weight * sample.maturityScore;
    weightSum += weight;
    nearest = Math.min(nearest, d);
  });

  return {
    value: clamp(weightSum ? weightedValue / weightSum : 0.5, 0, 1),
    uncertainty: clamp(nearest / Math.max((variogram?.rangeMeters ?? 0.75) * 1.2, 0.75), 0.05, 0.98),
    method: "inverse-distance fallback",
  };
}

function nearestSamples(samples, target, limit = 24) {
  return samples
    .slice()
    .sort((a, b) => distance(a, target) - distance(b, target))
    .slice(0, limit);
}

export function ordinaryKrigingPrediction(samples, target, variogram) {
  const points = nearestSamples(usableSamples(samples), target);
  if (points.length < 3) return inverseDistancePrediction(points, target, variogram);

  const size = points.length;
  const matrix = Array.from({ length: size + 1 }, () => Array(size + 1).fill(0));
  const rightSide = Array(size + 1).fill(0);

  for (let row = 0; row < size; row += 1) {
    const confidence = clamp(Number(points[row].confidence ?? 0.7), 0.1, 1);
    const evidence = clamp(Math.sqrt(sampleEvidenceCount(points[row])) / 4, 0.25, 1);
    const measurementNoise =
      variogram.nugget * (0.25 + (1 - confidence * evidence) * 0.75) + 1e-8;

    for (let column = 0; column < size; column += 1) {
      const gamma = sphericalVariogram(distance(points[row], points[column]), variogram);
      matrix[row][column] = row === column ? gamma + measurementNoise : gamma;
    }

    matrix[row][size] = 1;
    matrix[size][row] = 1;
    rightSide[row] = sphericalVariogram(distance(points[row], target), variogram);
  }

  rightSide[size] = 1;
  const solution = solveLinearSystem(matrix, rightSide);
  if (!solution) return inverseDistancePrediction(points, target, variogram);

  const weights = solution.slice(0, size);
  const lagrange = solution[size];
  const value = weights.reduce((sum, weight, index) => sum + weight * points[index].maturityScore, 0);
  const rawVariance = weights.reduce(
    (sum, weight, index) => sum + weight * rightSide[index],
    0,
  ) + lagrange;

  return {
    value: clamp(value, 0, 1),
    uncertainty: clamp(rawVariance / Math.max(variogram.sill + variogram.nugget, 0.001), 0.05, 0.98),
    method: "ordinary-kriging",
  };
}

function calculateSpatialReach(points, layout, variogram, diagnostics = {}) {
  const minimumSpan = Math.max(
    0.1,
    Math.min(Number(layout?.widthM) || 1, Number(layout?.heightM) || 1),
  );
  const neighbourSpacing = quantile(nearestNeighborDistances(points), 0.75) ?? 0;
  const moranSignal = clamp(((diagnostics?.moran?.value ?? 0) + 0.2) / 0.8, 0, 1);
  const gearySignal = clamp((1.3 - (diagnostics?.geary?.value ?? 1)) / 0.7, 0, 1);
  const pValue = diagnostics?.moranTest?.pValue;
  const permutationSignal =
    pValue == null ? 0.5 : pValue <= 0.05 ? 1 : pValue <= 0.1 ? 0.78 : pValue <= 0.2 ? 0.56 : 0.38;
  const structureScore = clamp(
    0.32 + moranSignal * 0.24 + gearySignal * 0.26 + permutationSignal * 0.18,
    0.3,
    0.92,
  );

  const baseRadiusM = Math.max(
    (variogram?.rangeMeters ?? minimumSpan * 0.3) * 0.9,
    neighbourSpacing * 7,
    minimumSpan * 0.22,
  );
  const influenceRadiusM = clamp(
    baseRadiusM * (0.86 + structureScore * 0.28),
    minimumSpan * 0.2,
    minimumSpan * 0.38,
  );

  return {
    influenceRadiusM,
    structureScore,
    neighbourSpacingM: neighbourSpacing,
    fadeToNeutralDistanceM: influenceRadiusM * 2.45,
  };
}

function calculateCoverageSupport(points, target, reach, cellSizeM) {
  if (!points.length) {
    return {
      nearestAnchorDistanceM: null,
      localEvidenceSupport: 0,
      nearAnchorSupport: 0,
      support: 0,
    };
  }

  let nearestAnchorDistanceM = Number.POSITIVE_INFINITY;
  let evidenceMass = 0;

  points.forEach((point) => {
    const d = distance(point, target);
    nearestAnchorDistanceM = Math.min(nearestAnchorDistanceM, d);
    const kernel = Math.exp(-0.5 * (d / Math.max(reach.influenceRadiusM, 0.001)) ** 2);
    const evidence = clamp(sampleEvidenceWeight(point), 0.1, 4);
    evidenceMass += kernel * evidence;
  });

  const densitySupport = 1 - Math.exp(-evidenceMass / 2.1);
  const nearestKernel = Math.exp(
    -0.5 * (nearestAnchorDistanceM / Math.max(reach.influenceRadiusM, 0.001)) ** 2,
  );
  const nearAnchorRatio =
    nearestAnchorDistanceM / Math.max(cellSizeM * 1.8, 0.05);
  const nearAnchorSupport = Math.exp(-(nearAnchorRatio ** 2));
  const support = clamp(
    Math.max(
      nearestKernel * (0.78 + densitySupport * 0.22),
      nearAnchorSupport,
    ),
    0,
    1,
  );

  return {
    nearestAnchorDistanceM,
    localEvidenceSupport: densitySupport,
    nearAnchorSupport,
    support,
  };
}

/*
 * The prediction value remains Ordinary Kriging. The displayed uncertainty is
 * calibrated from both Kriging variance and all-anchor coverage. This is what
 * lets the heatmap cover the complete observed tomato patch and fade to neutral
 * only as the data support approaches the "no local data" state.
 */
export function buildPredictionGrid(samples, layout, variogram, diagnostics = {}) {
  const points = usableSamples(samples);
  const minimumSpan = Math.max(0.1, Math.min(layout?.widthM ?? 1, layout?.heightM ?? 1));
  const cellSizeM = clamp(minimumSpan / 68, 0.045, 0.08);
  const minX = Number(layout?.minX ?? 0);
  const maxX = Number(layout?.maxX ?? 1);
  const minY = Number(layout?.minY ?? 0);
  const maxY = Number(layout?.maxY ?? 1);
  const reach = calculateSpatialReach(points, layout, variogram, diagnostics);
  const grid = [];

  for (let y = minY + cellSizeM / 2; y <= maxY; y += cellSizeM) {
    for (let x = minX + cellSizeM / 2; x <= maxX; x += cellSizeM) {
      if (!points.length) {
        grid.push({
          x,
          y,
          value: 0.5,
          uncertainty: 1,
          krigingUncertainty: 1,
          visualSupport: 0,
          nearestAnchorDistanceM: null,
          method: "no-anchor-data",
          cellSizeM,
          influenceRadiusM: reach.influenceRadiusM,
        });
        continue;
      }

      const target = { x, y };
      const prediction = ordinaryKrigingPrediction(points, target, variogram);
      const coverage = calculateCoverageSupport(points, target, reach, cellSizeM);
      const krigingUncertainty = clamp(prediction.uncertainty ?? 1, 0, 1);
      const coverageUncertainty = 1 - coverage.support;
      const uncertainty = clamp(
        coverageUncertainty * 0.82 + krigingUncertainty * 0.18,
        0.05,
        0.995,
      );

      grid.push({
        x,
        y,
        cellSizeM,
        nearestAnchorDistanceM: coverage.nearestAnchorDistanceM,
        localEvidenceSupport: coverage.localEvidenceSupport,
        visualSupport: 1 - uncertainty,
        uncertainty,
        krigingUncertainty,
        influenceRadiusM: reach.influenceRadiusM,
        spatialStructureScore: reach.structureScore,
        ...prediction,
        // Keep the combined uncertainty after spreading prediction fields.
        uncertainty,
        krigingUncertainty,
      });
    }
  }

  return grid;
}

export function getPredictionAt(grid = [], point) {
  if (!point || !Array.isArray(grid) || !grid.length) return null;

  return grid.reduce((closest, cell) => {
    if (!closest) return cell;
    return distance(cell, point) < distance(closest, point) ? cell : closest;
  }, null);
}

export function summarizeSpatialModel(samples, layout) {
  const support = buildKrigingSupportAnchors(samples, layout);
  const points = support.anchors;
  const variogram = estimateVariogram(points, layout);
  const thresholdM = clamp(variogram.rangeMeters * 1.25, 0.35, 2.4);
  const moran = calculateMoranI(points, thresholdM);
  const moranTest = calculateMoranPermutationTest(points, thresholdM);
  const geary = calculateGearyC(points, thresholdM);
  const grid = buildPredictionGrid(points, layout, variogram, {
    moran,
    moranTest,
    geary,
  });
  const areaM2 = Math.max((layout?.widthM ?? 1) * (layout?.heightM ?? 1), 0.01);
  const coverage = clamp(
    (grid.filter((cell) => cell.uncertainty < 0.95).length / Math.max(grid.length, 1)) * 100,
    0,
    100,
  );
  const heatmapReachM = median(grid.map((cell) => cell.influenceRadiusM)) ?? null;
  const fadeToNeutralDistanceM = heatmapReachM != null ? heatmapReachM * 2.45 : null;

  return {
    method: points.length >= 3 ? "ordinary-kriging" : "inverse-distance fallback",
    anchorCount: points.length,
    displayAnchorCount: support.displayAnchorCount,
    displayObservationCount: support.displayObservationCount,
    modelAnchorCount: points.length,
    modelAggregationCellM: support.cellSizeM,
    modelAnchors: points,
    moran,
    moranTest,
    geary,
    variogram,
    grid,
    heatmapCellSizeM: grid[0]?.cellSizeM ?? null,
    heatmapReachM,
    fadeToNeutralDistanceM,
    coverage,
    maturityAverage: mean(points.map((point) => point.maturityScore)),
    uncertaintyAverage: mean(grid.map((cell) => cell.uncertainty)),
    areaM2,
  };
}
