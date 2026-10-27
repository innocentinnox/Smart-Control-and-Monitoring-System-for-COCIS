import { NextResponse } from "next/server";
import { prisma } from "@/lib/db";
import { isFresh, verifySignature } from "@/lib/hmac";
import { batchSchema, ingestBatch } from "@/lib/ingest";

/**
 * POST /api/ingest (docs/event-schema.md 2.2).
 * Authenticate first (401), only then parse the body (400), so unsigned junk learns nothing.
 */
export async function POST(req: Request) {
  const deviceId = req.headers.get("x-device-id");
  const timestamp = req.headers.get("x-timestamp");
  const signature = req.headers.get("x-signature");
  // HMAC is over the exact bytes received, so read the raw text before parsing.
  const rawBody = await req.text();

  if (!deviceId || !timestamp || !signature) return fail(401, "missing auth headers");
  if (!isFresh(timestamp)) return fail(401, "stale or invalid timestamp");

  const device = await prisma.device.findUnique({ where: { id: deviceId } });
  if (!device || !verifySignature(device.secret, timestamp, rawBody, signature)) {
    return fail(401, "bad signature");
  }

  let json: unknown;
  try {
    json = JSON.parse(rawBody);
  } catch {
    return fail(400, "body is not JSON");
  }
  const parsed = batchSchema.safeParse(json);
  if (!parsed.success) {
    return NextResponse.json(
      { error: "invalid batch", issues: parsed.error.issues.slice(0, 10) },
      { status: 400 },
    );
  }
  if (parsed.data.deviceId !== deviceId) return fail(400, "deviceId does not match X-Device-Id");

  const result = await ingestBatch(deviceId, parsed.data.events);
  return NextResponse.json(result);
}

function fail(status: number, error: string) {
  return NextResponse.json({ error }, { status });
}
