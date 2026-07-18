import "server-only";

import { createHash } from "node:crypto";
import { promises as fs } from "node:fs";
import path from "node:path";
import { spawn } from "node:child_process";
import ffmpegPath from "ffmpeg-static";

const TRANSCODE_CACHE_DIR = path.join(process.cwd(), ".next-cache", "video-transcodes");
const activeTranscodes = new Map();

async function readFileStat(filePath) {
  try {
    const stat = await fs.stat(filePath);
    return stat.isFile() ? stat : null;
  } catch {
    return null;
  }
}

function outputNameFor(sourcePath, sourceStat) {
  const sourceBaseName = path
    .basename(sourcePath, path.extname(sourcePath))
    .replace(/[^A-Za-z0-9._-]/g, "_")
    .slice(0, 80) || "video";

  const fingerprint = createHash("sha256")
    .update(`${sourcePath}\0${sourceStat.size}\0${sourceStat.mtimeMs}`)
    .digest("hex")
    .slice(0, 20);

  return `${sourceBaseName}.${fingerprint}.h264.mp4`;
}

function runFfmpeg(inputPath, outputPath) {
  if (typeof ffmpegPath !== "string" || !ffmpegPath) {
    throw new Error(
      "ffmpeg-static did not provide an FFmpeg executable for this platform.",
    );
  }

  const args = [
    "-hide_banner",
    "-loglevel",
    "error",
    "-y",
    "-i",
    inputPath,
    "-map",
    "0:v:0",
    "-map",
    "0:a?",
    "-vf",
    "scale=trunc(iw/2)*2:trunc(ih/2)*2",
    "-c:v",
    "libx264",
    "-preset",
    "veryfast",
    "-crf",
    "23",
    "-pix_fmt",
    "yuv420p",
    "-c:a",
    "aac",
    "-b:a",
    "128k",
    "-movflags",
    "+faststart",
    outputPath,
  ];

  return new Promise((resolve, reject) => {
    const child = spawn(ffmpegPath, args, { windowsHide: true });
    let stderr = "";

    child.stderr?.on("data", (chunk) => {
      stderr += String(chunk);
    });

    child.once("error", (error) => {
      reject(new Error(`Could not start FFmpeg: ${error.message}`));
    });

    child.once("close", (code, signal) => {
      if (code === 0) {
        resolve();
        return;
      }

      const diagnostic = stderr.trim().slice(-2400);
      reject(
        new Error(
          `FFmpeg H.264 conversion failed${signal ? ` (${signal})` : ""}${
            diagnostic ? `: ${diagnostic}` : ""
          }`,
        ),
      );
    });
  });
}

async function transcodeVideo(sourcePath, outputPath) {
  await fs.mkdir(TRANSCODE_CACHE_DIR, { recursive: true });

  const temporaryPath = outputPath.replace(/\.mp4$/, ".part.mp4");
  await fs.rm(temporaryPath, { force: true });

  try {
    await runFfmpeg(sourcePath, temporaryPath);
    await fs.rename(temporaryPath, outputPath);
  } catch (error) {
    await fs.rm(temporaryPath, { force: true });
    throw error;
  }
}

export async function ensureBrowserCompatibleVideo(sourcePath) {
  const sourceStat = await readFileStat(sourcePath);
  if (!sourceStat) {
    throw new Error("Requested source video does not exist or is not a file.");
  }

  const outputPath = path.join(
    TRANSCODE_CACHE_DIR,
    outputNameFor(sourcePath, sourceStat),
  );
  const cachedStat = await readFileStat(outputPath);

  if (cachedStat && cachedStat.size > 0) {
    return {
      filePath: outputPath,
      fileName: path.basename(outputPath),
      sizeBytes: cachedStat.size,
      cacheHit: true,
    };
  }

  let transcode = activeTranscodes.get(outputPath);
  if (!transcode) {
    transcode = transcodeVideo(sourcePath, outputPath);
    activeTranscodes.set(outputPath, transcode);
  }

  try {
    await transcode;
  } finally {
    if (activeTranscodes.get(outputPath) === transcode) {
      activeTranscodes.delete(outputPath);
    }
  }

  const outputStat = await readFileStat(outputPath);
  if (!outputStat || outputStat.size <= 0) {
    throw new Error("FFmpeg completed without creating a playable H.264 video.");
  }

  return {
    filePath: outputPath,
    fileName: path.basename(outputPath),
    sizeBytes: outputStat.size,
    cacheHit: false,
  };
}
