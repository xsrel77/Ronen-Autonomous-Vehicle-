// src/app/api/_mockStore.js
// מצב משותף ל-mock endpoints (עובד טוב ב-dev).
// הערה: בפרודקשן/Serverless זה לא מובטח שיישמר בין קריאות.

export const store = {
  mode: "IDLE",          // IDLE | SCAN | PAUSE | MANUAL | ERROR
  battery: 92,           // %
  row: 1,                // 1..6
  progress: 0,           // 0..100
  speed: 0,              // m/s (או יחידה שתבחר)
  lastCommand: null,
  logs: [
    { ts: new Date().toISOString(), level: "INFO", msg: "Mock server started" },
  ],
};

export function pushLog(level, msg) {
  store.logs.unshift({
    ts: new Date().toISOString(),
    level,
    msg,
  });

  // לשמור רק 200 אחרונים
  if (store.logs.length > 200) store.logs.pop();
}

function clamp(n, min, max) {
  return Math.max(min, Math.min(max, n));
}

export function tickTelemetry() {
  // סימולציה קטנה: אם מצב SCAN — התקדמות עולה
  if (store.mode === "SCAN") {
    store.speed = 0.12;

    store.progress += 2; // ~2% לשניה
    if (store.progress >= 100) {
      store.progress = 0;
      store.row += 1;

      pushLog("INFO", `Finished row, moving to row ${store.row}`);

      if (store.row > 6) {
        store.row = 1;
        store.mode = "IDLE";
        store.speed = 0;
        pushLog("INFO", "Scan completed. Back to IDLE.");
      }
    }
  } else if (store.mode === "MANUAL") {
    // במצב ידני נשאיר מהירות כפי שפקודות MOVE קובעות (או 0)
  } else {
    store.speed = 0;
  }

  // סוללה יורדת לאט
  store.battery = clamp(store.battery - 0.01, 0, 100);
}
