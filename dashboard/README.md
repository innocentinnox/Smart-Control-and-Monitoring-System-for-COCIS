# CoCIS Smart Control: cloud dashboard

Next.js (App Router) + Prisma + Neon Postgres, deployable to Vercel. Receives the hub's signed
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
  db.ts                 Prisma client singleton
  hmac.ts               signature + timestamp checks
  ingest.ts             zod batch schema, idempotent insert, projections
  queries.ts            read queries used by the pages
  time.ts               EAT <-> UTC
prisma/schema.prisma    Event (raw log) + typed tables + Day-2 ScheduleConfig/Command
prisma/seed.ts          registers device hub-01
scripts/send-sample.mjs signs and POSTs a sample batch
```

## Setup

1. `npm install`
2. Create a Neon project, then copy `.env.example` to `.env` and fill in
   - `DATABASE_URL`: the **pooled** connection string (host contains `-pooler`)
   - `DIRECT_URL`: the **direct** connection string (used by Prisma for schema changes)
   - `DEVICE_SECRET`: the HMAC key for `hub-01`; must equal the one in the firmware.

   For a local Postgres, set both URLs to the same value, e.g.
   `postgresql://postgres:dev@localhost:5433/postgres`.
3. Create the tables: `npx prisma db push` (quick, Day 1), or
   `npx prisma migrate dev --name init` to start keeping migrations, then
   `npx prisma migrate deploy` in CI/production.
4. Seed the device: `npx prisma db seed`
5. Run: `npm run dev`, then open http://localhost:3000
6. Send test data: `node scripts/send-sample.mjs http://localhost:3000/api/ingest`
   - first run: `200 {"ackSeq":18,"inserted":18,"duplicates":0}`
   - second run: `200 {"ackSeq":18,"inserted":0,"duplicates":18}`
   - `--bad-sig`: `401 {"error":"bad signature"}`

> **Windows with Application Control (WDAC/Smart App Control):** if `next dev`/`next build` fails
> with "Turbopack is not supported ... native bindings are not available", the native SWC binary
> is being blocked. Use `npx next dev --webpack` / `npx next build --webpack`. Vercel is unaffected.

### Local Postgres with Docker

```bash
docker run -d --name cocis-pg -e POSTGRES_PASSWORD=dev -p 5433:5432 postgres:16-alpine
# ... steps 3-6 ...
docker rm -f cocis-pg
```

## Deploy to Vercel

1. Import the repo in Vercel and set **Root Directory** to `dashboard`.
2. Add env vars `DATABASE_URL` and `DIRECT_URL` (Neon's Vercel integration can do this).
3. Build command is `npm run build` (`prisma generate && next build`); `postinstall` also generates.
4. Apply the schema to Neon from your machine once (`npx prisma db push` or `migrate deploy`)
   and seed with a **real** secret: `DEVICE_SECRET=<long random> npx prisma db seed`.

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

Day-2 placeholders in the schema: `ScheduleConfig` (versioned per device) and `Command`
(`PENDING`/`ACKED`/`REJECTED`) for `GET /api/devices/:id/commands`, which is not built yet.
