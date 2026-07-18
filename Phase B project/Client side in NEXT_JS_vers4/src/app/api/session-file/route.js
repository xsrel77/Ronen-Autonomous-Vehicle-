import { promises as fs } from "node:fs";
import path from "node:path";
import { NextResponse } from "next/server";
import { resolveDashboardSessionFilePath } from "@/lib/dashboardSessionMapData";

export const dynamic = "force-dynamic";

const MIME_BY_EXT = {
  ".jpg": "image/jpeg",
  ".jpeg": "image/jpeg",
  ".png": "image/png",
  ".gif": "image/gif",
  ".webp": "image/webp",
  ".mp4": "video/mp4",
  ".m4v": "video/x-m4v",
  ".mov": "video/quicktime",
  ".webm": "video/webm",
  ".avi": "video/x-msvideo",
  ".mkv": "video/x-matroska",
  ".json": "application/json; charset=utf-8",
  ".jsonl": "application/x-ndjson; charset=utf-8",
  ".yaml": "text/yaml; charset=utf-8",
  ".yml": "text/yaml; charset=utf-8",
  ".pgm": "image/x-portable-graymap",
};

function parseRange(rangeHeader, fileSize) {
  if (!rangeHeader || !rangeHeader.startsWith("bytes=")) return null;

  const [startRaw, endRaw] = rangeHeader.replace("bytes=", "").split("-");
  const start = startRaw ? Number(startRaw) : 0;
  const end = endRaw ? Number(endRaw) : fileSize - 1;

  if (!Number.isFinite(start) || !Number.isFinite(end)) return null;
  if (start < 0 || end < start || start >= fileSize) return null;

  return {
    start,
    end: Math.min(end, fileSize - 1),
  };
}

export async function GET(request) {
  const { searchParams } = new URL(request.url);
  const sessionId = searchParams.get("session") || "";
  const relativePath = searchParams.get("path") || "";

  try {
    const filePath = resolveDashboardSessionFilePath(sessionId, relativePath);
    const stat = await fs.stat(filePath);
    if (!stat.isFile()) {
      return NextResponse.json({ error: "Requested session asset is not a file." }, { status: 404 });
    }

    const ext = path.extname(filePath).toLowerCase();
    const mimeType = MIME_BY_EXT[ext] || "application/octet-stream";
    const range = parseRange(request.headers.get("range"), stat.size);

    if (range) {
      const handle = await fs.open(filePath, "r");
      const length = range.end - range.start + 1;
      const buffer = Buffer.alloc(length);
      await handle.read(buffer, 0, length, range.start);
      await handle.close();

      return new Response(buffer, {
        status: 206,
        headers: {
          "Content-Type": mimeType,
          "Content-Length": String(length),
          "Content-Range": `bytes ${range.start}-${range.end}/${stat.size}`,
          "Accept-Ranges": "bytes",
          "Cache-Control": "no-store",
        },
      });
    }

    const data = await fs.readFile(filePath);
    return new Response(data, {
      headers: {
        "Content-Type": mimeType,
        "Content-Length": String(stat.size),
        "Accept-Ranges": "bytes",
        "Cache-Control": "no-store",
      },
    });
  } catch (error) {
    return NextResponse.json(
      {
        error: error instanceof Error ? error.message : "Failed to read session file",
      },
      { status: 404 },
    );
  }
}
