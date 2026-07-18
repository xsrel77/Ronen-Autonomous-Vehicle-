import { buildEnvAnalysisPayload, getRobotDebugSources } from "@/lib/robotDebugData";
import { NextResponse } from "next/server";

export const dynamic = "force-dynamic";

export async function GET() {
  try {
    return Response.json(await buildEnvAnalysisPayload());
  } catch (error) {
    return NextResponse.json(
      {
        error:
          error instanceof Error ? error.message : "Failed to load ENV analysis",
        sources: getRobotDebugSources(),
      },
      { status: 500 },
    );
  }
}
