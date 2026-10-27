import type { Metadata } from "next";
import Link from "next/link";
import type { ReactNode } from "react";
import { AutoRefresh } from "@/components/AutoRefresh";
import "./globals.css";

export const metadata: Metadata = {
  title: "CoCIS Smart Control",
  description: "Access control and room lighting dashboard for CoCIS, Makerere University",
};

const links = [
  { href: "/", label: "Overview" },
  { href: "/access", label: "Access log" },
  { href: "/energy", label: "Energy" },
];

export default function RootLayout({ children }: { children: ReactNode }) {
  return (
    <html lang="en">
      <body>
        <header className="border-b border-slate-200 bg-white">
          <nav className="mx-auto flex max-w-6xl flex-wrap items-center gap-6 px-4 py-3">
            <span className="font-semibold">CoCIS Smart Control</span>
            {links.map((l) => (
              <Link key={l.href} href={l.href} className="text-sm text-slate-600 hover:text-slate-900">
                {l.label}
              </Link>
            ))}
            <span className="ml-auto text-xs text-slate-400">auto-refresh 5 s</span>
          </nav>
        </header>
        <main className="mx-auto max-w-6xl px-4 py-6">{children}</main>
        <AutoRefresh seconds={5} />
      </body>
    </html>
  );
}
