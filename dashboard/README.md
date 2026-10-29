# CoCIS Smart Control: cloud dashboard

Next.js 16 (App Router) + Prisma 7 + Neon Postgres, deployable to Vercel. Receives the hub's signed
event batches on `POST /api/ingest` and shows who is inside, room state, alerts, the access log
and per-room energy. The binding contract is [`../docs/event-schema.md`](../docs/event-schema.md).

```
app/
  api/ingest/route.ts   thin handler: auth -> validate -> ingest
  page.tsx              overview (people inside, rooms, alerts, devices)
  access/page.tsx       access log, filters ?result=granted|denied&side=IN|OUT
  energy/page.tsx       latest on_wh / saved_wh per room
  layout.tsx            nav + 5 s auto-refresh
components/             AutoRefresh (router.refresh), small UI pieces
lib/
  db.ts                 Prisma client (node-postgres driver adapter), one per process
  generated/prisma/     generated Prisma client (git-ignored, made by `prisma generate`)
  hmac.ts               signature + timestamp checks
  ingest.ts             zod batch schema, idempotent insert, projections
  queries.ts            read queries used by the pages
  time.ts               EAT <-> UTC
prisma.config.ts        Prisma 7 config: schema, migrations, seed, CLI connection
prisma/schema.prisma    Event (raw log) + typed tables (access, rooms, lights, energy, alerts)
prisma/migrations/      SQL migrations, applied with `npm run db:migrate`
prisma/seed.ts          registers device hub-01
scripts/send-sample.mjs signs and POSTs a sample batch
```

## Setup

1. `npm install` (also runs `prisma generate`). npm 11 asks before running package install
   scripts; the ones Prisma and tsx need are already approved under `allowScripts` in
   `package.json`.
2. Copy `.env.example` to `.env` and set `DATABASE_URL` to Neon's **pooled** string and
   `DEVICE_SECRET` to a random value (the command is in `.env.example`). Prisma 7 reads `.env`
   through `prisma.config.ts`; migrations go over the direct (non-pooler) host, derived
   automatically or taken from `DATABASE_URL_UNPOOLED`.
3. Create the tables: `npm run db:migrate` (`prisma migrate deploy`, no shadow database needed).
4. Seed the device: `npm run db:seed`
5. Run: `npm run dev`, then open http://localhost:3000
6. Send test data: `npm run send-sample` (or `node scripts/send-sample.mjs <url>`)
   - first run: `200 {"ackSeq":18,"inserted":18,"duplicates":0}`
   - second run: `200 {"ackSeq":18,"inserted":0,"duplicates":18}`
   - `--bad-sig`: `401 {"error":"bad signature"}`

> **Windows with Application Control (WDAC/Smart App Control):** if `next dev`/`next build` fails
> with "Turbopack is not supported ... native bindings are not available", the native SWC binary
> is being blocked. Use `npx next dev --webpack` / `npx next build --webpack`. Vercel is unaffected.

### Changing the schema

`prisma migrate dev` needs a shadow database to check the migration history. Use a throwaway
Postgres in Docker instead of a second Neon database:

```bash
npm run db:shadow                        # postgres:16-alpine on localhost:55432
export SHADOW_DATABASE_URL="postgresql://postgres:shadow@localhost:55432/postgres"
npm run db:migrate:dev -- --name <change>   # writes prisma/migrations/<timestamp>_<change>/
npm run db:shadow:stop
```

To check that the migrations match the schema without touching any real database:
`npx prisma migrate diff --from-migrations prisma/migrations --to-schema prisma/schema.prisma --exit-code`
(with `SHADOW_DATABASE_URL` set as above).

## Deploy to Vercel

1. Push the repository to GitHub, then in Vercel: **Add New → Project**, import it, and set
   **Root Directory** to `dashboard`. Framework, build (`npm run build` = `prisma generate &&
   next build`) and output settings are detected.
2. In the Neon store page (Vercel → Storage → the database), **Connect to Project** and pick this
   project: it adds `DATABASE_URL` (and `DATABASE_URL_UNPOOLED`) to its environment variables.
3. Deploy. The schema and the device are already in the database (`npm run db:migrate` and
   `npm run db:seed` run from your machine against the same Neon database); for later schema
   changes run `npm run db:migrate` again before or after deploying.
4. Point the sample sender (or the hub) at the deployment:
   `node scripts/send-sample.mjs https://<project>.vercel.app/api/ingest`

**The device secret has two ends.** `npm run db:seed` stores `DEVICE_SECRET` in the `hub-01`
row; `/api/ingest` checks every request against that row, so Vercel needs no copy of the secret.
Whatever sends events signs with the same value: today `scripts/send-sample.mjs` (it reads
`.env`), later the hub's uploader. To change the secret, edit `.env` and run `npm run db:seed`.

## API contract (summary of event-schema.md 2.2)

```http
POST /api/ingest
Content-Type: application/json
X-Device-Id: hub-01
X-Timestamp: <unix seconds>           # must be within ±300 s of server time
X-Signature: <hex HMAC-SHA256(key = device secret, msg = timestamp + "." + raw body)>

{ "deviceId": "hub-01",
  "events": [ { "seq": 42, "ts": "2026-10-27T09:15:02", "type": "ACCESS",
                "data": { "uid": "01020304", "name": "Alice", "role": "STUDENT",
                          "side": "IN", "ok": 1, "reason": "OK" },
                "hash": "9f2c...e1" } ] }
```

- `ts` is hub local time (Africa/Kampala, UTC+3); stored as UTC, shown as EAT.
- `type` is one of `BOOT ACCESS ROOM LIGHT ENERGY CONFIG USER AUTH ALERT`. Every event goes into
  `Event`; `ACCESS`, `ROOM`, `LIGHT`, `ENERGY`, `ALERT` are also projected into typed tables.
  `ok`, `occupied`, `room`, `level`, `on_wh`, `saved_wh` are numbers. 1–500 events per batch.
- `(deviceId, seq)` is unique. Re-sent events are counted as duplicates and change nothing.

| Status | Body | When |
|---|---|---|
| 200 | `{ "ackSeq": n, "inserted": i, "duplicates": d }` | stored; `ackSeq` = highest seq in the batch, so the hub can advance its outbox cursor |
| 401 | `{ "error": ... }` | missing headers, stale timestamp, unknown device or bad signature |
| 400 | `{ "error": ..., "issues"? }` | (only after a valid signature) body not JSON, schema violation, body `deviceId` ≠ header |
