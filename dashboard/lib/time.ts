/** The hub keeps local time (Africa/Kampala, EAT = UTC+3, no DST); the DB stores UTC. */
export const TIME_ZONE = "Africa/Kampala";

/** "2026-10-27T09:15:02" (EAT) -> Date. */
export function eatToDate(ts: string): Date {
  return new Date(`${ts}+03:00`);
}

const fmt = new Intl.DateTimeFormat("en-GB", {
  timeZone: TIME_ZONE,
  dateStyle: "medium",
  timeStyle: "medium",
});

/** Render a stored timestamp back in campus time. */
export function formatEat(d: Date): string {
  return fmt.format(d);
}
