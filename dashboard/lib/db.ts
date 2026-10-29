import { PrismaPg } from "@prisma/adapter-pg";
import { PrismaClient } from "./generated/prisma/client";

/**
 * Prisma 7 talks to Postgres through a driver adapter (node-postgres here). DATABASE_URL is
 * Neon's pooled endpoint, the right one for serverless functions that open many short
 * connections. Migrations use the direct endpoint instead (prisma.config.ts).
 */
function createClient() {
  // Neon's strings say sslmode=require, which node-postgres already treats as verify-full (it
  // checks the certificate) and warns will change meaning in pg 9. Ask for verify-full outright.
  const connectionString = process.env.DATABASE_URL?.replace("sslmode=require", "sslmode=verify-full");
  return new PrismaClient({ adapter: new PrismaPg({ connectionString }) });
}

/** One client per process; reused across hot reloads in dev so connections don't pile up. */
const globalForPrisma = globalThis as unknown as { prisma?: PrismaClient };

export const prisma = globalForPrisma.prisma ?? createClient();

if (process.env.NODE_ENV !== "production") globalForPrisma.prisma = prisma;
