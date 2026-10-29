// Prisma 7 configuration: where the schema, migrations and seed live, and which database the
// CLI (migrate, db seed, studio) talks to. The app itself connects through the driver adapter
// in lib/db.ts, not through this file.
import "dotenv/config"; // Prisma 7 no longer loads .env by itself
import { defineConfig } from "prisma/config";

/**
 * Migrations need a direct connection: Neon's pooled endpoint (host "-pooler") runs PgBouncer
 * in transaction mode, which the schema engine cannot use. Vercel's Neon integration provides
 * DATABASE_URL_UNPOOLED; locally it is derived from the pooled DATABASE_URL.
 */
function directUrl(): string {
  const pooled = process.env.DATABASE_URL ?? "";
  return process.env.DATABASE_URL_UNPOOLED ?? pooled.replace("-pooler.", ".");
}

export default defineConfig({
  schema: "prisma/schema.prisma",
  migrations: {
    path: "prisma/migrations",
    seed: "tsx prisma/seed.ts",
  },
  datasource: {
    url: directUrl(),
    // Only `prisma migrate dev` needs a shadow database: `npm run db:shadow` starts a throwaway
    // Postgres in Docker for it. `migrate deploy` (what production uses) never does.
    shadowDatabaseUrl: process.env.SHADOW_DATABASE_URL,
  },
});
