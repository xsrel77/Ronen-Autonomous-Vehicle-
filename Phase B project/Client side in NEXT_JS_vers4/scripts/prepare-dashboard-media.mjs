import { promises as fs } from "node:fs";
import path from "node:path";
import { spawn } from "node:child_process";
import { createRequire } from "node:module";

const require = createRequire(import.meta.url);
const projectRoot = process.cwd();
const sessionRoot = process.env.ROBOT_DASHBOARD_SESSION_ROOT || path.join(projectRoot, "src", "session-data");
const publicMediaRoot = path.join(projectRoot, "public", "session-media");
const VIDEO_EXTS = new Set([".mp4", ".mov", ".m4v", ".avi", ".mkv", ".webm"]);

async function pathExists(filePath) {
  try {
    await fs.access(filePath);
    return true;
  } catch {
    return false;
  }
}

async function statOrNull(filePath) {
  try {
    return await fs.stat(filePath);
  } catch {
    return null;
  }
}

function resolveFfmpegBin() {
  try {
    const ffmpegStatic = require("ffmpeg-static");
    if (typeof ffmpegStatic === "string" && ffmpegStatic.trim()) {
      return ffmpegStatic;
    }
  } catch {
    // The package is optional for local development. Fall back to PATH.
  }
  return process.env.FFMPEG_BIN || "ffmpeg";
}

function isBrowserReadyName(fileName) {
  const lower = fileName.toLowerCase();
  return lower.endsWith(".webm") || lower.includes("browser") || lower.includes("h264");
}

function browserFileName(sourceName) {
  const ext = path.extname(sourceName).toLowerCase();
  const base = path.basename(sourceName, ext);
  if (isBrowserReadyName(sourceName)) return sourceName;
  return `${base}_browser.mp4`;
}

function runFfmpeg(ffmpegBin, sourcePath, targetPath) {
  return new Promise((resolve, reject) => {
    const args = [
      "-y",
      "-i",
      sourcePath,
      "-c:v",
      "libx264",
      "-preset",
      "veryfast",
      "-crf",
      "23",
      "-pix_fmt",
      "yuv420p",
      "-movflags",
      "+faststart",
      "-an",
      targetPath,
    ];

    const child = spawn(ffmpegBin, args, { stdio: ["ignore", "pipe", "pipe"] });
    let stderr = "";
    child.stderr.on("data", (chunk) => {
      stderr += chunk.toString();
    });
    child.on("error", reject);
    child.on("close", (code) => {
      if (code === 0) resolve();
      else reject(new Error(stderr || `ffmpeg exited with code ${code}`));
    });
  });
}

async function copyIfBrowserReady(sourcePath, targetPath) {
  await fs.mkdir(path.dirname(targetPath), { recursive: true });
  await fs.copyFile(sourcePath, targetPath);
}

async function targetNeedsUpdate(sourcePath, targetPath) {
  const [sourceStat, targetStat] = await Promise.all([statOrNull(sourcePath), statOrNull(targetPath)]);
  if (!sourceStat) return false;
  if (!targetStat) return true;
  if (targetStat.size <= 0) return true;
  return sourceStat.mtimeMs > targetStat.mtimeMs;
}

async function prepareSessionVideos(ffmpegBin, sessionDirName) {
  const videosDir = path.join(sessionRoot, sessionDirName, "videos");
  if (!(await pathExists(videosDir))) return { sessionDirName, prepared: 0, skipped: 0, failed: 0 };

  const entries = await fs.readdir(videosDir, { withFileTypes: true });
  let prepared = 0;
  let skipped = 0;
  let failed = 0;

  for (const entry of entries) {
    if (!entry.isFile()) continue;
    const ext = path.extname(entry.name).toLowerCase();
    if (!VIDEO_EXTS.has(ext)) continue;
    if (entry.name.toLowerCase().includes("_browser") && ext === ".mp4") continue;

    const sourcePath = path.join(videosDir, entry.name);
    const outputName = browserFileName(entry.name);
    const targetPath = path.join(publicMediaRoot, sessionDirName, "videos", outputName);

    if (!(await targetNeedsUpdate(sourcePath, targetPath))) {
      skipped += 1;
      continue;
    }

    try {
      await fs.mkdir(path.dirname(targetPath), { recursive: true });
      if (isBrowserReadyName(entry.name)) {
        await copyIfBrowserReady(sourcePath, targetPath);
      } else {
        await runFfmpeg(ffmpegBin, sourcePath, targetPath);
      }
      prepared += 1;
      console.log(`[dashboard-media] prepared ${path.relative(projectRoot, targetPath)}`);
    } catch (error) {
      failed += 1;
      console.warn(`[dashboard-media] failed ${path.relative(projectRoot, sourcePath)}: ${error?.message || error}`);
    }
  }

  return { sessionDirName, prepared, skipped, failed };
}

async function main() {
  if (!(await pathExists(sessionRoot))) {
    console.log(`[dashboard-media] no session root found: ${sessionRoot}`);
    return;
  }

  const ffmpegBin = resolveFfmpegBin();
  const entries = await fs.readdir(sessionRoot, { withFileTypes: true });
  const sessions = entries.filter((entry) => entry.isDirectory() && entry.name.startsWith("session_")).map((entry) => entry.name);

  if (!sessions.length) {
    console.log("[dashboard-media] no dashboard sessions found");
    return;
  }

  let totalPrepared = 0;
  let totalSkipped = 0;
  let totalFailed = 0;

  for (const sessionName of sessions) {
    const result = await prepareSessionVideos(ffmpegBin, sessionName);
    totalPrepared += result.prepared;
    totalSkipped += result.skipped;
    totalFailed += result.failed;
  }

  console.log(`[dashboard-media] done. prepared=${totalPrepared}, skipped=${totalSkipped}, failed=${totalFailed}`);

  if (totalFailed > 0) {
    console.warn("[dashboard-media] some videos could not be prepared. The dashboard will still work, but those videos may not play in the browser.");
  }
}

main().catch((error) => {
  console.error(`[dashboard-media] fatal: ${error?.message || error}`);
  process.exitCode = 1;
});
