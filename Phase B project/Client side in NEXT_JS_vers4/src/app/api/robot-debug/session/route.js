import { NextResponse } from "next/server";
import {
  listRobotSessions,
  readRobotSession,
} from "@/lib/robotSessionData";

export const runtime = "nodejs";
export const dynamic = "force-dynamic";

export async function GET(request) {
  try {
    const sessions = await listRobotSessions();
    const { searchParams } = new URL(request.url);
    const requestedSessionId = searchParams.get("session");
    const selectedSessionId = requestedSessionId ?? sessions[0]?.id ?? null;

    if (!selectedSessionId) {
      return NextResponse.json({
        sessions: [],
        selectedSessionId: null,
        session: null,
        latest: null,
        timeline: [],
        detectionEvents: [],
        lidarPreview: null,
        message: "No robot sessions were found in src/session-data.",
      });
    }

    const selectedExists = sessions.some(
      (session) => session.id === selectedSessionId,
    );

    if (!selectedExists) {
      return NextResponse.json(
        {
          error: `Robot session not found: ${selectedSessionId}`,
          sessions,
        },
        { status: 404 },
      );
    }

    const data = await readRobotSession(selectedSessionId);

    return NextResponse.json({
      sessions,
      selectedSessionId,
      session: data.session,
      latest: data.latest,
      timeline: data.timeline,
      detectionEvents: data.detectionEvents,
      lidarPreview: data.lidarPreview,
    });
  } catch (error) {
    return NextResponse.json(
      {
        error:
          error instanceof Error
            ? error.message
            : "Failed to load robot session data.",
      },
      { status: 500 },
    );
  }
}
