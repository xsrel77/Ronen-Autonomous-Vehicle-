function finite(value, fallback = null) {
  if (value == null || value === "") return fallback;
  const number = Number(value);
  return Number.isFinite(number) ? number : fallback;
}

function mean(values) {
  const usable = values.filter((value) => Number.isFinite(value));
  return usable.length ? usable.reduce((sum, value) => sum + value, 0) / usable.length : null;
}

function standardDeviation(values, average) {
  const usable = values.filter((value) => Number.isFinite(value));
  if (usable.length < 2 || !Number.isFinite(average)) return null;
  const variance = usable.reduce((sum, value) => sum + (value - average) ** 2, 0) / (usable.length - 1);
  const result = Math.sqrt(variance);
  return result > 1e-9 ? result : null;
}

function dot(a, b) {
  return a.reduce((sum, value, index) => sum + value * b[index], 0);
}

function normalize(vector) {
  const length = Math.sqrt(dot(vector, vector)) || 1;
  return vector.map((value) => value / length);
}

function matrixVectorProduct(matrix, vector) {
  return matrix.map((row) => dot(row, vector));
}

function covarianceMatrix(scaledRows) {
  const rowCount = scaledRows.length;
  const columnCount = scaledRows[0]?.length ?? 0;
  const matrix = Array.from({ length: columnCount }, () => Array(columnCount).fill(0));

  for (let row = 0; row < columnCount; row += 1) {
    for (let column = row; column < columnCount; column += 1) {
      let value = 0;
      for (const sample of scaledRows) value += sample[row] * sample[column];
      value /= Math.max(1, rowCount - 1);
      matrix[row][column] = value;
      matrix[column][row] = value;
    }
  }

  return matrix;
}

function powerIteration(matrix, offset = 0) {
  const size = matrix.length;
  let vector = normalize(Array.from({ length: size }, (_, index) => 1 + ((index + offset) % 3) * 0.19));

  for (let iteration = 0; iteration < 140; iteration += 1) {
    const next = normalize(matrixVectorProduct(matrix, vector));
    const difference = Math.sqrt(next.reduce((sum, value, index) => sum + (value - vector[index]) ** 2, 0));
    vector = next;
    if (difference < 1e-9) break;
  }

  return {
    eigenvector: vector,
    eigenvalue: Math.max(0, dot(vector, matrixVectorProduct(matrix, vector))),
  };
}

function deflate(matrix, eigenvalue, eigenvector) {
  return matrix.map((row, rowIndex) => row.map((value, columnIndex) => value - eigenvalue * eigenvector[rowIndex] * eigenvector[columnIndex]));
}

function nearestEnvironment(sample, environmentSeries, firstTomatoTimestamp, lastTomatoTimestamp) {
  if (!environmentSeries.length) return null;
  if (environmentSeries.length === 1) return environmentSeries[0];

  const span = Math.max(1, lastTomatoTimestamp - firstTomatoTimestamp);
  const sampleTimestamp = finite(sample.firstTimestampMs ?? sample.timestampMs, firstTomatoTimestamp);
  const progress = (sampleTimestamp - firstTomatoTimestamp) / span;
  const minEnvTime = finite(environmentSeries[0]?.tSec, 0);
  const maxEnvTime = finite(environmentSeries.at(-1)?.tSec, environmentSeries.length - 1);
  const targetTime = minEnvTime + progress * Math.max(1, maxEnvTime - minEnvTime);

  return environmentSeries.reduce((best, candidate) => {
    const bestDelta = Math.abs(finite(best?.tSec, 0) - targetTime);
    const candidateDelta = Math.abs(finite(candidate?.tSec, 0) - targetTime);
    return candidateDelta < bestDelta ? candidate : best;
  }, environmentSeries[0]);
}

export const PCA_VARIABLES = [
  { key: "tempC", label: "Temperature", unit: "°C", group: "Microclimate gradient" },
  { key: "humidityPct", label: "Humidity", unit: "%", group: "Microclimate gradient" },
  { key: "pressureHpa", label: "Pressure", unit: "hPa", group: "Microclimate gradient" },
  { key: "gasKohm", label: "Gas resistance", unit: "kΩ", group: "Microclimate gradient" },
  { key: "x", label: "X location", unit: "m", group: "Spatial position gradient" },
  { key: "y", label: "Y location", unit: "m", group: "Spatial position gradient" },
  { key: "timeMin", label: "Time", unit: "min", group: "Temporal gradient" },
  { key: "maturityScore", label: "Maturity index", unit: "0–1", group: "Tomato maturity gradient" },
  { key: "count", label: "Observation count", unit: "count", group: "Tomato maturity gradient" },
];

function interpretComponents(loadings, componentCount) {
  return Array.from({ length: componentCount }, (_, componentIndex) => {
    const componentKey = `pc${componentIndex + 1}`;
    const ranked = loadings
      .map((loading) => ({ ...loading, strength: Math.abs(loading[componentKey] ?? 0) }))
      .sort((a, b) => b.strength - a.strength);
    const strengthsByGroup = new Map();

    ranked.slice(0, 4).forEach((loading) => {
      strengthsByGroup.set(loading.group, (strengthsByGroup.get(loading.group) ?? 0) + loading.strength);
    });

    const groups = [...strengthsByGroup.entries()].sort((a, b) => b[1] - a[1]);
    const primary = groups[0]?.[0] ?? "Mixed PCA gradient";
    const secondary = groups[1]?.[0] ?? null;
    const label = secondary && (groups[1][1] >= groups[0][1] * 0.65) ? `${primary} + ${secondary}` : primary;

    return {
      id: `PC${componentIndex + 1}`,
      componentKey,
      label,
      explanation: `This component is driven most strongly by the available selected-session variables in ${label.toLowerCase()}.`,
      strongest: ranked.slice(0, 4),
    };
  });
}

export function buildPcaDataset(environmentSeries = [], tomatoSamples = []) {
  const samples = tomatoSamples.filter((sample) => Number.isFinite(sample?.x) && Number.isFinite(sample?.y));
  if (!samples.length) return [];

  const tomatoTimes = samples.map((sample) => finite(sample.firstTimestampMs ?? sample.timestampMs, 0));
  const firstTomatoTimestamp = Math.min(...tomatoTimes);
  const lastTomatoTimestamp = Math.max(...tomatoTimes);

  return samples.map((sample, index) => {
    const environment = nearestEnvironment(sample, environmentSeries, firstTomatoTimestamp, lastTomatoTimestamp) ?? {};
    return {
      id: sample.id ?? `landmark-${index + 1}`,
      label: sample.label ?? "Tomato landmark",
      tempC: finite(environment.tempC),
      humidityPct: finite(environment.humidityPct),
      pressureHpa: finite(environment.pressureHpa),
      gasKohm: finite(environment.gasKohm),
      x: finite(sample.x),
      y: finite(sample.y),
      timeMin: (finite(sample.firstTimestampMs ?? sample.timestampMs, firstTomatoTimestamp) - firstTomatoTimestamp) / 60_000,
      maturityScore: finite(sample.maturityScore),
      count: finite(sample.observationCount ?? sample.count, 1),
      confidence: finite(sample.confidence),
    };
  });
}

function usableVariables(rows) {
  return PCA_VARIABLES.filter((variable) => {
    const values = rows.map((row) => finite(row[variable.key])).filter((value) => value != null);
    const average = mean(values);
    return values.length >= 2 && standardDeviation(values, average) != null;
  });
}

export function calculatePca(environmentSeries = [], tomatoSamples = [], maxComponents = 3) {
  const rows = buildPcaDataset(environmentSeries, tomatoSamples);
  const variables = usableVariables(rows);

  if (rows.length < 3) {
    return {
      ready: false,
      reason: "At least three real selected-session tomato landmarks are required for PCA.",
      rows,
      variables,
      components: [],
      scores: [],
      loadings: [],
      explainedVariance: [],
      cumulativeVariance: [],
      componentInterpretations: [],
    };
  }

  if (variables.length < 2) {
    return {
      ready: false,
      reason: "The selected session does not contain two varying numeric variables after missing sensor channels are excluded.",
      rows,
      variables,
      components: [],
      scores: [],
      loadings: [],
      explainedVariance: [],
      cumulativeVariance: [],
      componentInterpretations: [],
    };
  }

  const columns = variables.map((variable) => rows.map((row) => finite(row[variable.key])));
  const means = columns.map((column) => mean(column));
  const deviations = columns.map((column, index) => standardDeviation(column, means[index]) ?? 1);

  // Missing values are imputed only with that variable's observed mean. A completely
  // missing channel (for example gas resistance in these sessions) was removed above.
  const scaledRows = rows.map((row) => variables.map((variable, index) => {
    const value = finite(row[variable.key], means[index]);
    return (value - means[index]) / deviations[index];
  }));

  let covariance = covarianceMatrix(scaledRows);
  const totalVariance = Math.max(1e-9, covariance.reduce((sum, row, index) => sum + row[index], 0));
  const componentCount = Math.min(maxComponents, variables.length, rows.length - 1);
  const components = [];

  for (let index = 0; index < componentCount; index += 1) {
    const component = powerIteration(covariance, index);
    components.push({
      id: `PC${index + 1}`,
      ...component,
      explainedRatio: component.eigenvalue / totalVariance,
    });
    covariance = deflate(covariance, component.eigenvalue, component.eigenvector);
  }

  const scores = scaledRows.map((row, index) => {
    const values = components.map((component) => dot(row, component.eigenvector));
    return {
      id: rows[index].id,
      label: rows[index].label,
      maturityScore: rows[index].maturityScore,
      x: rows[index].x,
      y: rows[index].y,
      pc1: values[0] ?? 0,
      pc2: values[1] ?? 0,
      pc3: values[2] ?? 0,
    };
  });

  const loadings = variables.map((variable, variableIndex) => {
    const loading = { ...variable };
    components.forEach((component, componentIndex) => {
      loading[`pc${componentIndex + 1}`] = component.eigenvector[variableIndex] * Math.sqrt(component.eigenvalue);
    });
    return loading;
  });

  const explainedVariance = components.map((component) => component.explainedRatio);
  let cumulative = 0;
  const cumulativeVariance = explainedVariance.map((value) => {
    cumulative += value;
    return cumulative;
  });

  return {
    ready: true,
    reason: null,
    rows,
    variables,
    components,
    scores,
    loadings,
    explainedVariance,
    cumulativeVariance,
    componentInterpretations: interpretComponents(loadings, components.length),
  };
}

export function strongestLoadings(loadings = [], componentKey = "pc1", limit = 3) {
  return [...loadings]
    .sort((a, b) => Math.abs(b[componentKey] ?? 0) - Math.abs(a[componentKey] ?? 0))
    .slice(0, limit);
}
