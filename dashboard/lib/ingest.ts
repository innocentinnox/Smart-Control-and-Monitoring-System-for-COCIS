import { z } from "zod";
import type { Prisma } from "@prisma/client";
import { prisma } from "./db";
import { eatToDate } from "./time";

/** Largest batch accepted in one request; the hub's outbox sends far fewer. */
export const MAX_BATCH = 500;

const base = {
  seq: z.number().int().nonnegative(),
  ts: z.string().regex(/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}$/, "expected YYYY-MM-DDTHH:MM:SS"),
  hash: z.string().max(128).optional(),
};

/** 0/1 on the wire, boolean in the DB. */
const flag = z.union([z.literal(0), z.literal(1)]).transform((v) => v === 1);
const room = z.number().int().min(1).max(2);

// Projected types get a strict data shape; looseObject keeps any extra keys in the raw log.
const accessData = z.looseObject({
  uid: z.string().regex(/^[0-9A-F]{8}$/),
  name: z.string(),
  role: z.string(),
  side: z.enum(["IN", "OUT"]),
  ok: flag,
  reason: z.enum([
    "OK", "OK_PRIVILEGED", "OK_ESCORT", "UNKNOWN_CARD", "DISABLED",
    "OUTSIDE_HOURS", "ANTI_PASSBACK", "LOCKDOWN",
  ]),
});
const roomData = z.looseObject({ room, occupied: flag });
const lightData = z.looseObject({
  room,
  level: z.number().int().min(0).max(100),
  mode: z.enum(["AUTO", "ON", "OFF"]),
});
const energyData = z.looseObject({
  room,
  on_wh: z.number().nonnegative(),
  saved_wh: z.number().nonnegative(),
});
const alertData = z.looseObject({ code: z.string(), detail: z.string() });
/** BOOT/CONFIG/USER/AUTH are only stored raw for now. */
const anyData = z.record(z.string(), z.unknown());

const eventSchema = z.discriminatedUnion("type", [
  z.object({ ...base, type: z.literal("ACCESS"), data: accessData }),
  z.object({ ...base, type: z.literal("ROOM"), data: roomData }),
  z.object({ ...base, type: z.literal("LIGHT"), data: lightData }),
  z.object({ ...base, type: z.literal("ENERGY"), data: energyData }),
  z.object({ ...base, type: z.literal("ALERT"), data: alertData }),
  z.object({ ...base, type: z.enum(["BOOT", "CONFIG", "USER", "AUTH"]), data: anyData }),
]);

export const batchSchema = z.object({
  deviceId: z.string().min(1),
  events: z.array(eventSchema).min(1).max(MAX_BATCH),
});

export type Batch = z.infer<typeof batchSchema>;
type IngestEvent = Batch["events"][number];

export interface IngestResult {
  ackSeq: number;
  inserted: number;
  duplicates: number;
}

/**
 * Store a verified batch. Rows already present (same deviceId+seq) are skipped, and only
 * newly stored events are projected, so a re-sent batch changes nothing. skipDuplicates
 * on every table also covers two overlapping requests racing each other.
 */
export async function ingestBatch(deviceId: string, events: IngestEvent[]): Promise<IngestResult> {
  const seqs = events.map((e) => e.seq);
  const ackSeq = Math.max(...seqs);

  return prisma.$transaction(async (tx) => {
    const existing = await tx.event.findMany({
      where: { deviceId, seq: { in: seqs } },
      select: { seq: true },
    });
    const seen = new Set(existing.map((e) => e.seq));
    // De-duplicate within the batch too, keeping the first copy.
    const fresh = events.filter((e) => !seen.has(e.seq) && seen.add(e.seq));

    const { count: inserted } = await tx.event.createMany({
      data: fresh.map((e) => ({
        deviceId,
        seq: e.seq,
        ts: eatToDate(e.ts),
        type: e.type,
        data: e.data as Prisma.InputJsonObject,
        hash: e.hash,
      })),
      skipDuplicates: true,
    });

    await project(tx, deviceId, fresh);

    const device = await tx.device.findUniqueOrThrow({ where: { id: deviceId } });
    await tx.device.update({
      where: { id: deviceId },
      data: { lastSeenAt: new Date(), lastSeq: Math.max(device.lastSeq ?? -1, ackSeq) },
    });

    return { ackSeq, inserted, duplicates: events.length - inserted };
  });
}

/** Copy each new event into its typed table. */
async function project(tx: Prisma.TransactionClient, deviceId: string, events: IngestEvent[]) {
  const access: Prisma.AccessEventCreateManyInput[] = [];
  const rooms: Prisma.RoomEventCreateManyInput[] = [];
  const lights: Prisma.LightEventCreateManyInput[] = [];
  const energy: Prisma.EnergySampleCreateManyInput[] = [];
  const alerts: Prisma.AlertCreateManyInput[] = [];

  for (const e of events) {
    const key = { deviceId, seq: e.seq, ts: eatToDate(e.ts) };
    switch (e.type) {
      case "ACCESS": {
        const { uid, name, role, side, ok, reason } = e.data;
        access.push({ ...key, uid, name, role, side, ok, reason });
        break;
      }
      case "ROOM":
        rooms.push({ ...key, room: e.data.room, occupied: e.data.occupied });
        break;
      case "LIGHT":
        lights.push({ ...key, room: e.data.room, level: e.data.level, mode: e.data.mode });
        break;
      case "ENERGY":
        energy.push({ ...key, room: e.data.room, onWh: e.data.on_wh, savedWh: e.data.saved_wh });
        break;
      case "ALERT":
        alerts.push({ ...key, code: e.data.code, detail: e.data.detail });
        break;
    }
  }

  const opts = { skipDuplicates: true } as const;
  if (access.length) await tx.accessEvent.createMany({ data: access, ...opts });
  if (rooms.length) await tx.roomEvent.createMany({ data: rooms, ...opts });
  if (lights.length) await tx.lightEvent.createMany({ data: lights, ...opts });
  if (energy.length) await tx.energySample.createMany({ data: energy, ...opts });
  if (alerts.length) await tx.alert.createMany({ data: alerts, ...opts });
}
