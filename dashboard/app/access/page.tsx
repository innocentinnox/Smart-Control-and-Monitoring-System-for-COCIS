import Link from "next/link";
import { Badge, Card, Empty } from "@/components/ui";
import { accessLog, type AccessFilter } from "@/lib/queries";
import { formatEat } from "@/lib/time";

export const dynamic = "force-dynamic";

type Params = { result?: string; side?: string };

/** Unknown query values are ignored rather than rejected, so a mistyped URL still shows the log. */
function toFilter({ result, side }: Params): AccessFilter {
  return {
    ok: result === "granted" ? true : result === "denied" ? false : undefined,
    side: side === "IN" || side === "OUT" ? side : undefined,
  };
}

export default async function AccessPage({ searchParams }: { searchParams: Promise<Params> }) {
  const params = await searchParams;
  const rows = await accessLog(toFilter(params));

  return (
    <Card title="Access log">
      <div className="mb-4 flex flex-wrap gap-6 text-sm">
        <FilterGroup label="Result" name="result" options={["granted", "denied"]} params={params} />
        <FilterGroup label="Side" name="side" options={["IN", "OUT"]} params={params} />
      </div>
      {rows.length === 0 ? (
        <Empty>No matching scans.</Empty>
      ) : (
        <div className="overflow-x-auto">
          <table className="w-full text-left text-sm">
            <thead className="border-b border-slate-200 text-xs uppercase text-slate-500">
              <tr>
                <th className="py-2 pr-4">Time (EAT)</th>
                <th className="py-2 pr-4">Name</th>
                <th className="py-2 pr-4">UID</th>
                <th className="py-2 pr-4">Role</th>
                <th className="py-2 pr-4">Side</th>
                <th className="py-2 pr-4">Result</th>
                <th className="py-2">Reason</th>
              </tr>
            </thead>
            <tbody className="divide-y divide-slate-100">
              {rows.map((r) => (
                <tr key={r.id}>
                  <td className="py-2 pr-4 whitespace-nowrap text-slate-500">{formatEat(r.ts)}</td>
                  <td className="py-2 pr-4 font-medium">{r.name}</td>
                  <td className="py-2 pr-4 font-mono text-xs">{r.uid}</td>
                  <td className="py-2 pr-4">{r.role}</td>
                  <td className="py-2 pr-4">{r.side}</td>
                  <td className="py-2 pr-4">
                    <Badge tone={r.ok ? "green" : "red"}>{r.ok ? "granted" : "denied"}</Badge>
                  </td>
                  <td className="py-2 text-slate-600">{r.reason}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}
    </Card>
  );
}

/** Link-based filter chips: works without client JS and keeps the other filter in the URL. */
function FilterGroup({
  label,
  name,
  options,
  params,
}: {
  label: string;
  name: keyof Params;
  options: string[];
  params: Params;
}) {
  const href = (value?: string) => {
    const q = new URLSearchParams();
    for (const [k, v] of Object.entries({ ...params, [name]: value })) if (v) q.set(k, v);
    const s = q.toString();
    return s ? `/access?${s}` : "/access";
  };
  const chip = (value: string | undefined, text: string) => (
    <Link
      key={text}
      href={href(value)}
      className={`rounded-full px-3 py-1 ${
        params[name] === value ? "bg-slate-900 text-white" : "bg-slate-100 text-slate-700 hover:bg-slate-200"
      }`}
    >
      {text}
    </Link>
  );
  return (
    <div className="flex items-center gap-2">
      <span className="text-slate-500">{label}:</span>
      {chip(undefined, "all")}
      {options.map((o) => chip(o, o))}
    </div>
  );
}
