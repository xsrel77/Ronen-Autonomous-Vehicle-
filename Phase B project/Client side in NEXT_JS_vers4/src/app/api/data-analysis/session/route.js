import { NextResponse } from "next/server";
import { buildDataAnalysisSessionPayload } from "@/lib/dataAnalysisSessionData";

export const runtime = "nodejs";
export const dynamic = "force-dynamic";

export async function GET(request) {
  const { searchParams } = new URL(request.url);
  const sessionId = searchParams.get("session") || "";

  try {
    return NextResponse.json(await buildDataAnalysisSessionPayload(sessionId));
  } catch (error) {
    return NextResponse.json(
      {
        error:
          error instanceof Error
            ? error.message
            : "Failed to read the selected data-analysis session.",
      },
      { status: 500 },
    );
  }
}
