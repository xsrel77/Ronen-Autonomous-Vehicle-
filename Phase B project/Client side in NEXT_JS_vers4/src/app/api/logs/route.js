import { NextResponse } from "next/server";
import { store } from "../_mockStore";

export const dynamic = "force-dynamic";

export async function GET() {
  return NextResponse.json(store.logs);
}
