import type { Prisma } from "@prisma/client";
import { prisma } from "./db";

export const ROOMS = [1, 2] as const;

const newestFirst = [{ ts: "desc" }, { seq: "desc" }] satisfies Prisma.AccessEventOrderByWithRelationInput[];

/**
 * Muster list: a person is inside when their most recent *granted* scan was at the IN door.
 * Denied scans don't move anyone, so they are excluded before picking the latest per uid.
 */
export async function peopleInside() {
  const latest = await prisma.accessEvent.findMany({
    where: { ok: true },
    orderBy: newestFirst,
    distinct: ["uid"],
  });
  return latest.filter((a) => a.side === "IN");
}

/** Latest occupancy, light and energy reading for each room. */
export async function roomStatus() {
  const [rooms, lights, energy] = await Promise.all([
    prisma.roomEvent.findMany({ orderBy: newestFirst, distinct: ["room"] }),
    prisma.lightEvent.findMany({ orderBy: newestFirst, distinct: ["room"] }),
    prisma.energySample.findMany({ orderBy: newestFirst, distinct: ["room"] }),
  ]);
  return ROOMS.map((room) => ({
    room,
    occupancy: rooms.find((r) => r.room === room),
    light: lights.find((l) => l.room === room),
    energy: energy.find((e) => e.room === room),
  }));
}

export function latestAlerts(take = 10) {
  return prisma.alert.findMany({ orderBy: newestFirst, take });
}

export interface AccessFilter {
  ok?: boolean;
  side?: "IN" | "OUT";
}

export function accessLog(filter: AccessFilter, take = 200) {
  return prisma.accessEvent.findMany({ where: filter, orderBy: newestFirst, take });
}

export function devices() {
  return prisma.device.findMany({
    select: { id: true, name: true, lastSeenAt: true, lastSeq: true },
    orderBy: { id: "asc" },
  });
}
