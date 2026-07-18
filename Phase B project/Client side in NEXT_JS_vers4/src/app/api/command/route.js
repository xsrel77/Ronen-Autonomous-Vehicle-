import { NextResponse } from "next/server";
import { store, pushLog } from "../_mockStore";

export const dynamic = "force-dynamic";

export async function POST(req) {
  let cmd = null;

  try {
    cmd = await req.json();
  } catch {
    return NextResponse.json({ ok: false, error: "Invalid JSON" }, { status: 400 });
  }

  store.lastCommand = cmd;

  // פענוח פקודות בסיסי
  if (cmd?.type === "SET_MODE") {
    store.mode = cmd.mode || "IDLE";
    pushLog("INFO", `SET_MODE -> ${store.mode}`);
  } else if (cmd?.type === "STOP") {
    store.mode = "IDLE";
    store.speed = 0;
    pushLog("WARN", "STOP");
  } else if (cmd?.type === "START_SCAN") {
    store.mode = "SCAN";
    store.row = Array.isArray(cmd.rows) && cmd.rows.length ? cmd.rows[0] : store.row;
    store.progress = 0;
    pushLog("INFO", `START_SCAN rows=${JSON.stringify(cmd.rows || [])}`);
  } else if (cmd?.type === "MOVE") {
    // במוק: רק נשנה מהירות ונהפוך ל-MANUAL
    store.mode = "MANUAL";
    store.speed = Math.max(0, Number(cmd.value || 0)) / 100; // 0..1
    pushLog("INFO", `MOVE ${cmd.direction} value=${cmd.value}`);
  } else if (cmd?.type === "SET_CAMERA") {
    pushLog("INFO", `SET_CAMERA pan=${cmd.pan} tilt=${cmd.tilt}`);
  } else {
    pushLog("WARN", `Unknown command: ${JSON.stringify(cmd)}`);
  }

  return NextResponse.json({ ok: true });
}
