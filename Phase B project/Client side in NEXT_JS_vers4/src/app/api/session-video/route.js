import { promises as fs } from "node:fs";
import path from "node:path";
import { NextResponse } from "next/server";
import { resolveDashboardSessionFilePath } from "@/lib/dashboardSessionMapData";
import { ensureBrowserCompatibleVideo } from "@/lib/videoEncoder";

export const runtime = "nodejs";
export const dynamic = "force-dynamic";

const VIDEO_EXTENSIONS = new Set([".mp4", ".webm", ".mov", ".m4v", ".avi", ".mkv"]);

function parseRange(rangeHeader, fileSize) {
  if (!rangeHeader || !rangeHeader.startsWith("bytes=")) return null;

  const match = /^bytes=(\d*)-(\d*)$/.exec(rangeHeader.trim());
  if (!match) return null;

  const [, startRaw, endRaw] = match;
  if (!startRaw && !endRaw) return null;

  if (!startRaw) {
    const suffixLength = Number(endRaw);
    if (!Number.isFinite(suffixLength) || suffixLength <= 0) return null;

    return {
      start: Math.max(0, fileSize - suffixLength),
      end: fileSize - 1,
    };
  }

  const start = Number(startRaw);
  const requestedEnd = endRaw ? Number(endRaw) : fileSize - 1;

  if (
    !Number.isFinite(start) ||
    !Number.isFinite(requestedEnd) ||
    start < 0 ||
    requestedEnd < start ||
    start >= fileSize
  ) {
    return null;
  }

  return {
    start,
    end: Math.min(requestedEnd, fileSize - 1),
  };
}

async function createVideoResponse(request, video) {
  const range = parseRange(request.headers.get("range"), video.sizeBytes);
  const headers = {
    "Content-Type": "video/mp4",
    "Accept-Ranges": "bytes",
    "Cache-Control": "private, max-age=300",
    "Content-Disposition": `inline; filename="${video.fileName.replaceAll('"', "")}"`,
    "X-Robot-Video-Codec": "h264",
    "X-Robot-Video-Cache": video.cacheHit ? "hit" : "miss",
  };

  if (range) {
    const length = range.end - range.start + 1;
    const file = await fs.open(video.filePath, "r");

    try {
      const content = Buffer.alloc(length);
      await file.read(content, 0, length, range.start);

      return new Response(content, {
        status: 206,
        headers: {
          ...headers,
          "Content-Length": String(length),
          "Content-Range": `bytes ${range.start}-${range.end}/${video.sizeBytes}`,
        },
      });
    } finally {
      await file.close();
    }
  }

  const content = await fs.readFile(video.filePath);
  return new Response(content, {
    headers: {
      ...headers,
      "Content-Length": String(video.sizeBytes),
    },
  });
}

export async function GET(request) {
  const { searchParams } = new URL(request.url);
  const sessionId = searchParams.get("session") || "";
  const relativePath = searchParams.get("path") || "";

  try {
    const sourcePath = resolveDashboardSessionFilePath(sessionId, relativePath);
    const extension = path.extname(sourcePath).toLowerCase();

    if (!VIDEO_EXTENSIONS.has(extension)) {
      return NextResponse.json(
        { error: "Requested session asset is not a supported video file." },
        { status: 415 },
      );
    }

    const video = await ensureBrowserCompatibleVideo(sourcePath);
    return createVideoResponse(request, video);
  } catch (error) {
    const message =
      error instanceof Error ? error.message : "Failed to prepare browser-compatible video.";

    const status = /Invalid session|Invalid session file|not a supported video/i.test(message)
      ? 400
      : /does not exist|not a file|ENOENT/i.test(message)
        ? 404
        : 500;

    return NextResponse.json({ error: message }, { status });
  }
}
