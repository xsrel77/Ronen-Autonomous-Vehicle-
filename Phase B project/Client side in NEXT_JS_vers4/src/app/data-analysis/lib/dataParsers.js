export function safeNumber(value, fallback = null) {
  const n = Number(value);
  return Number.isFinite(n) ? n : fallback;
}

export function normalizeEnvSeries(payload) {
  const raw = payload?.series ?? payload?.samples ?? payload?.data ?? [];
  if (!Array.isArray(raw)) return [];

  return raw
    .map((item, index) => ({
      index,
      timestampMs: safeNumber(item.timestampMs ?? item.timestamp_ms ?? item.timeMs ?? item.t_ms),
      tSec: safeNumber(item.tSec ?? item.t_sec ?? item.timeSec ?? item.time_s, index),
      tempC: safeNumber(item.tempC ?? item.temp_c ?? item.temperatureC ?? item.temperature),
      humidityPct: safeNumber(item.humidityPct ?? item.humidity_pct ?? item.humidity),
      pressureHpa: safeNumber(item.pressureHpa ?? item.pressure_hpa ?? item.pressure),
      gasKohm: safeNumber(item.gasKohm ?? item.gas_kohm ?? item.gas),
    }))
    .filter((row) => [row.tempC, row.humidityPct, row.pressureHpa, row.gasKohm].some((v) => v !== null));
}

export function buildFallbackEnvSeries() {
  return [
    { index: 0, tSec: 0, tempC: 25.6, humidityPct: 61.1, pressureHpa: 1009.2, gasKohm: 84.1 },
    { index: 1, tSec: 60, tempC: 26.1, humidityPct: 60.4, pressureHpa: 1009.0, gasKohm: 86.4 },
    { index: 2, tSec: 120, tempC: 26.8, humidityPct: 59.9, pressureHpa: 1008.8, gasKohm: 88.2 },
    { index: 3, tSec: 180, tempC: 27.2, humidityPct: 58.7, pressureHpa: 1008.5, gasKohm: 90.5 },
    { index: 4, tSec: 240, tempC: 27.5, humidityPct: 58.1, pressureHpa: 1008.3, gasKohm: 91.7 },
  ];
}

export function formatTimeFromSeconds(seconds) {
  const s = Math.max(0, Math.round(safeNumber(seconds, 0)));
  if (s < 60) return `${s}s`;
  if (s < 3600) return `${Math.floor(s / 60)}m ${s % 60}s`;
  return `${Math.floor(s / 3600)}h ${Math.floor((s % 3600) / 60)}m`;
}
