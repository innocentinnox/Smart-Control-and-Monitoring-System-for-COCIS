import { createHmac, timingSafeEqual } from "node:crypto";

/** Requests whose X-Timestamp is further than this from server time are replays (event-schema 2.2). */
export const MAX_SKEW_SECONDS = 300;

/** hex HMAC-SHA256(secret, `${timestamp}.${rawBody}`), the same string the firmware signs. */
export function sign(secret: string, timestamp: string, rawBody: string): string {
  return createHmac("sha256", secret).update(`${timestamp}.${rawBody}`).digest("hex");
}

/** Constant-time check so the signature can't be guessed byte by byte from response timing. */
export function verifySignature(
  secret: string,
  timestamp: string,
  rawBody: string,
  signature: string,
): boolean {
  if (!/^[0-9a-f]{64}$/i.test(signature)) return false;
  const expected = Buffer.from(sign(secret, timestamp, rawBody), "hex");
  return timingSafeEqual(expected, Buffer.from(signature, "hex"));
}

/** True when the unix-seconds timestamp is within the allowed clock skew. */
export function isFresh(timestamp: string, nowMs = Date.now()): boolean {
  if (!/^\d{1,12}$/.test(timestamp)) return false;
  return Math.abs(nowMs / 1000 - Number(timestamp)) <= MAX_SKEW_SECONDS;
}
