import { PrismaClient } from "@prisma/client";

/** One client per process; reused across hot reloads in dev so connections don't pile up. */
const globalForPrisma = globalThis as unknown as { prisma?: PrismaClient };

export const prisma = globalForPrisma.prisma ?? new PrismaClient();

if (process.env.NODE_ENV !== "production") globalForPrisma.prisma = prisma;
