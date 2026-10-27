import { Card, Empty } from "@/components/ui";
import { roomStatus } from "@/lib/queries";
import { formatEat } from "@/lib/time";

export const dynamic = "force-dynamic";

export default async function EnergyPage() {
  const rooms = await roomStatus();
  const samples = rooms.flatMap(({ energy }) => (energy ? [energy] : []));
  // Bars share one scale so rooms can be compared at a glance.
  const max = Math.max(1, ...samples.map((e) => e.onWh + e.savedWh));

  return (
    <Card title="Energy per room (since hub boot)">
      {samples.length === 0 ? (
        <Empty>No energy samples yet.</Empty>
      ) : (
        <div className="space-y-6">
          {samples.map(({ room, onWh, savedWh, ts }) => {
            const savedPct = Math.round((savedWh / Math.max(onWh + savedWh, 1e-9)) * 100);
            return (
              <div key={room}>
                <div className="mb-1 flex justify-between text-sm">
                  <span className="font-medium">Room {room}</span>
                  <span className="text-slate-500">
                    used {onWh.toFixed(1)} Wh · saved {savedWh.toFixed(1)} Wh ({savedPct}%) · {formatEat(ts)}
                  </span>
                </div>
                <div className="flex h-4 overflow-hidden rounded bg-slate-100">
                  <div className="bg-amber-400" style={{ width: `${(onWh / max) * 100}%` }} title="used" />
                  <div className="bg-emerald-500" style={{ width: `${(savedWh / max) * 100}%` }} title="saved" />
                </div>
              </div>
            );
          })}
          <p className="flex gap-4 text-xs text-slate-500">
            <span><span className="mr-1 inline-block h-2 w-2 rounded-sm bg-amber-400" />used</span>
            <span><span className="mr-1 inline-block h-2 w-2 rounded-sm bg-emerald-500" />saved by auto-dimming</span>
          </p>
        </div>
      )}
    </Card>
  );
}
