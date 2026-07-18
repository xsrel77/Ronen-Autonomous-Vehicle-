async function requestJson(path, options = {}) {
  const res = await fetch(path, {
    cache: "no-store",
    ...options,
    headers: {
      "Content-Type": "application/json",
      ...(options.headers || {}),
    },
  });

  if (!res.ok) {
    let details = "";
    try {
      details = await res.text();
    } catch {}
    throw new Error(
      `API error ${res.status} on ${path}${details ? `: ${details}` : ""}`,
    );
  }

  return res.json();
}

export async function fetchTelemetry() {
  return requestJson("/api/telemetry");
}

export async function fetchLogs() {
  const raw = await requestJson("/api/logs");
  return Array.isArray(raw) ? raw : [];
}

export async function sendCommand(cmd) {
  return requestJson("/api/command", {
    method: "POST",
    body: JSON.stringify(cmd),
  });
}

export async function fetchDetections() {
  const raw = await requestJson("/api/detections");
  return Array.isArray(raw) ? raw : [];
}

export async function fetchMap(sessionId = "") {
  const qs = sessionId ? `?session=${encodeURIComponent(sessionId)}` : "";
  return requestJson(`/api/map${qs}`);
}

export async function fetchDashboardSessions() {
  return requestJson("/api/dashboard-sessions");
}

export async function fetchEnvAnalysis() {
  return requestJson("/api/env-analysis");
}
