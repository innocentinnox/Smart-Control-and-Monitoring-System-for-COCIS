#!/usr/bin/env node
// Signs and POSTs a realistic batch to /api/ingest, exactly as the hub's cloud_sync task will.
// Usage: node scripts/send-sample.mjs [url] [--bad-sig]
//   env DEVICE_ID (default hub-01), DEVICE_SECRET (default the dev secret from .env.example)
// seq numbers are fixed, so running it twice must report duplicates, not errors.
import { createHash, createHmac } from "node:crypto";

const args = process.argv.slice(2);
const url = args.find((a) => !a.startsWith("--")) ?? "http://localhost:3000/api/ingest";
const badSig = args.includes("--bad-sig");
const deviceId = process.env.DEVICE_ID ?? "hub-01";
const secret = process.env.DEVICE_SECRET ?? "dev-secret-hub-01-change-me";

// Today's date in EAT so the dashboard shows "today".
const day = new Date(Date.now() + 3 * 3600_000).toISOString().slice(0, 10);
const at = (hms) => `${day}T${hms}`;

const sample = [
  ["07:58:10", "BOOT", { reason: "POWER_ON" }],
  ["08:02:11", "ACCESS", { uid: "01020304", name: "Alice", role: "STUDENT", side: "IN", ok: 1, reason: "OK" }],
  ["08:02:14", "ROOM", { room: 1, occupied: 1, daylight: 0, lux: 90 }],
  ["08:02:15", "LIGHT", { room: 1, level: 100, mode: "AUTO", lux: 90 }],
  ["08:05:40", "ACCESS", { uid: "11223344", name: "Bob", role: "STAFF", side: "IN", ok: 1, reason: "OK" }],
  ["08:07:03", "ACCESS", { uid: "C0FFEE99", name: "unknown", role: "NONE", side: "IN", ok: 0, reason: "UNKNOWN_CARD" }],
  ["08:09:30", "ACCESS", { uid: "11223344", name: "Bob", role: "STAFF", side: "IN", ok: 0, reason: "ANTI_PASSBACK" }],
  ["08:10:00", "ENERGY", { room: 1, on_wh: 12.4, saved_wh: 5.1 }],
  ["08:10:00", "ENERGY", { room: 2, on_wh: 3.2, saved_wh: 9.8 }],
  ["08:12:47", "ACCESS", { uid: "55667788", name: "Carol", role: "SECURITY", side: "IN", ok: 1, reason: "OK_PRIVILEGED" }],
  ["08:13:05", "ROOM", { room: 2, occupied: 1, daylight: 0, lux: 120 }],
  ["08:13:06", "LIGHT", { room: 2, level: 100, mode: "AUTO", lux: 120 }],
  ["08:20:19", "ACCESS", { uid: "01020304", name: "Alice", role: "STUDENT", side: "OUT", ok: 1, reason: "OK" }],
  ["08:20:30", "ROOM", { room: 1, occupied: 0, daylight: 0, lux: 90 }],
  ["08:20:31", "LIGHT", { room: 1, level: 0, mode: "AUTO", lux: 90 }],
  ["08:25:00", "ALERT", { code: "NODE_OFFLINE", detail: "ROOM2" }],
  ["08:25:42", "ALERT", { code: "NODE_ONLINE", detail: "ROOM2" }],
  ["08:30:00", "ACCESS", { uid: "AABBCCDD", name: "Admin", role: "ADMIN", side: "IN", ok: 1, reason: "OK" }],
];

// Same hash chain as the SD log (event-schema 2.1), so the cloud copy is auditable too.
let prev = "0".repeat(64);
const events = sample.map(([hms, type, data], i) => {
  const seq = i + 1;
  const kv = Object.entries(data).map(([k, v]) => `${k}=${v}`).join(" ");
  const line = `${String(seq).padStart(6, "0")}|${at(hms)}|${type}|${kv}`;
  prev = createHash("sha256").update(`${prev}|${line}`).digest("hex");
  return { seq, ts: at(hms), type, data, hash: prev };
});

const body = JSON.stringify({ deviceId, events });
const timestamp = String(Math.floor(Date.now() / 1000));
const signature = badSig
  ? "0".repeat(64)
  : createHmac("sha256", secret).update(`${timestamp}.${body}`).digest("hex");

const res = await fetch(url, {
  method: "POST",
  headers: {
    "Content-Type": "application/json",
    "X-Device-Id": deviceId,
    "X-Timestamp": timestamp,
    "X-Signature": signature,
  },
  body,
});
console.log(res.status, await res.text());
process.exitCode = res.ok ? 0 : 1;
