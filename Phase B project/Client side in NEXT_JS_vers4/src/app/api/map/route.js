import {
  buildDashboardRosMapPayload,
  getDashboardRosMapSources,
} from "@/lib/dashboardSessionMapData";
import {
  buildMapPayload,
  getRobotDebugSources,
  readRealtimeDebugFrame,
} from "@/lib/robotDebugData";
import { NextResponse } from "next/server";

export const dynamic = "force-dynamic";

export async function GET(request) {
  const { searchParams } = new URL(request.url);
  const sessionId = searchParams.get("session") || "";

  try {
    const ros2Map = await buildDashboardRosMapPayload(sessionId);
    if (ros2Map) return Response.json(ros2Map);
  } catch (error) {
    return NextResponse.json(
      {
        error:
          error instanceof Error
            ? error.message
            : "Failed to load ROS2 dashboard session map",
        sources: getDashboardRosMapSources(),
      },
      { status: 500 },
    );
  }

  try {
    const frame = await readRealtimeDebugFrame();
    return Response.json(
      buildMapPayload({
        snapshot: frame.current,
        history: frame.history,
        fileUpdatedAt: frame.fileUpdatedAt,
      }),
    );
  } catch (error) {
    return NextResponse.json(
      {
        error: error instanceof Error ? error.message : "Failed to load map",
        sources: {
          dashboardSession: getDashboardRosMapSources(),
          legacyDebug: getRobotDebugSources(),
        },
      },
      { status: 500 },
    );
  }
}
