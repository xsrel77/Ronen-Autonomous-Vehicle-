import path from "node:path";
import { promises as fs } from "node:fs";
import { NextResponse } from "next/server";
import { resolveRobotSessionMediaFile } from "@/lib/robotSessionData";

export const runtime = "nodejs";
export const dynamic = "force-dynamic";

const MIME_TYPES = {
  ".jpg": "image/jpeg",
  ".jpeg": "image/jpeg",
  ".png": "image/png",
  ".webp": "image/webp",
  ".mp4": "video/mp4",
};

function mediaTypeForFile(fileName) {
  return MIME_TYPES[path.extname(fileName).toLowerCase()] ?? null;
}

export async function GET(request) {
  try {
    const { searchParams } = new URL(request.url);
    const sessionId = searchParams.get("session");
    const relativePath = searchParams.get("path");

    const media = await resolveRobotSessionMediaFile(sessionId, relativePath);
    const contentType = mediaTypeForFile(media.fileName);

    if (!contentType) {
      return NextResponse.json(
        { error: "Unsupported robot-session media type." },
        { status: 415 },
      );
    }

    const content = await fs.readFile(media.filePath);

    return new NextResponse(content, {
      headers: {
        "Content-Type": contentType,
        "Content-Length": String(media.sizeBytes),
        "Cache-Control": "private, max-age=300",
        "Content-Disposition": `inline; filename="${media.fileName.replaceAll('"', "")}"`,
      },
    });
  } catch (error) {
    const message =
      error instanceof Error ? error.message : "Failed to load robot-session media.";

    const status = /Invalid|not allowed|required|Unsupported/.test(message)
      ? 400
      : /ENOENT|Could not read media file/.test(message)
        ? 404
        : 500;

    return NextResponse.json({ error: message }, { status });
  }
}
