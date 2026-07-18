import { listDashboardSessions, getDashboardRosMapSources } from "@/lib/dashboardSessionMapData";
import { NextResponse } from "next/server";

export const dynamic = "force-dynamic";

export async function GET() {
  try {
    const sessions = await listDashboardSessions();
    return Response.json({
      kind: "rbv2_dashboard_sessions",
      sessions,
      selected: sessions[0]?.id ?? null,
    });
  } catch (error) {
    return NextResponse.json(
      {
        error: error instanceof Error ? error.message : "Failed to list dashboard sessions",
        sources: getDashboardRosMapSources(),
      },
      { status: 500 },
    );
  }
}
