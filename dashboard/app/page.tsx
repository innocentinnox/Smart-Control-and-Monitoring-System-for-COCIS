import { Badge, Card, Empty } from "@/components/ui";
import { devices, latestAlerts, peopleInside, roomStatus } from "@/lib/queries";
import { formatEat } from "@/lib/time";

// Live data: never prerender at build time.
export const dynamic = "force-dynamic";

export default async function Overview() {
  const [inside, rooms, alerts, devs] = await Promise.all([
    peopleInside(),
    roomStatus(),
    latestAlerts(),
    devices(),
  ]);

  return (
    <div className="grid gap-6 md:grid-cols-2">
      <Card title={`People inside (${inside.length})`}>
        {inside.length === 0 ? (
          <Empty>Nobody is inside.</Empty>
        ) : (
          <ul className="divide-y divide-slate-100">
            {inside.map((p) => (
              <li key={p.uid} className="flex items-center justify-between py-2 text-sm">
                <span>
                  <span className="font-medium">{p.name}</span>{" "}
                  <span className="font-mono text-xs text-slate-400">{p.uid}</span>
                </span>
                <span className="flex items-center gap-3">
                  <Badge tone="slate">{p.role}</Badge>
                  <span className="text-xs text-slate-500">since {formatEat(p.ts)}</span>
                </span>
              </li>
            ))}
          </ul>
        )}
      </Card>

      <Card title="Rooms">
        <div className="grid grid-cols-2 gap-4">
          {rooms.map(({ room, occupancy, light }) => (
            <div key={room} className="rounded-lg border border-slate-100 p-4">
              <div className="mb-2 flex items-center justify-between">
                <span className="font-medium">Room {room}</span>
                {occupancy ? (
                  <Badge tone={occupancy.occupied ? "green" : "slate"}>
                    {occupancy.occupied ? "occupied" : "vacant"}
                  </Badge>
                ) : (
                  <Badge tone="slate">no data</Badge>
                )}
              </div>
              <div className="text-sm text-slate-600">
                Light {light ? `${light.level}% (${light.mode})` : "–"}
              </div>
              <div className="text-xs text-slate-500">
                {occupancy?.lux != null ? `${occupancy.lux} lux` : "light level –"}
                {occupancy?.daylight != null && (occupancy.daylight ? " · daylight, lamp not needed" : " · dark")}
              </div>
              <div className="mt-2 h-2 rounded bg-slate-100">
                <div className="h-2 rounded bg-amber-400" style={{ width: `${light?.level ?? 0}%` }} />
              </div>
            </div>
          ))}
        </div>
      </Card>

      <Card title="Latest alerts">
        {alerts.length === 0 ? (
          <Empty>No alerts.</Empty>
        ) : (
          <ul className="divide-y divide-slate-100">
            {alerts.map((a) => (
              <li key={a.id} className="flex items-center justify-between py-2 text-sm">
                <span>
                  <Badge tone="red">{a.code}</Badge> <span className="text-slate-600">{a.detail}</span>
                </span>
                <span className="text-xs text-slate-500">{formatEat(a.ts)}</span>
              </li>
            ))}
          </ul>
        )}
      </Card>

      <Card title="Devices">
        {devs.length === 0 ? (
          <Empty>No devices registered. Run the seed.</Empty>
        ) : (
          <ul className="divide-y divide-slate-100">
            {devs.map((d) => (
              <li key={d.id} className="flex items-center justify-between py-2 text-sm">
                <span>
                  <span className="font-medium">{d.name}</span>{" "}
                  <span className="font-mono text-xs text-slate-400">{d.id}</span>
                </span>
                <span className="text-xs text-slate-500">
                  {d.lastSeenAt ? `seen ${formatEat(d.lastSeenAt)}, seq ${d.lastSeq}` : "never seen"}
                </span>
              </li>
            ))}
          </ul>
        )}
      </Card>
    </div>
  );
}
