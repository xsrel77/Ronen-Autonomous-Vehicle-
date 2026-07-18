"use client";

import { useEffect, useState } from "react";
import "./globals.css";
import Nav from "@/components/Nav";

export default function RootLayout({ children }) {
  const [navOpen, setNavOpen] = useState(true);

  useEffect(() => {
    try {
      const raw = localStorage.getItem("ui:navOpen");
      if (raw === null) return;

      const restoreNavState = () => setNavOpen(raw === "true");
      const frameId = window.requestAnimationFrame(restoreNavState);

      return () => window.cancelAnimationFrame(frameId);
    } catch {}
  }, []);

  useEffect(() => {
    try {
      localStorage.setItem("ui:navOpen", String(navOpen));
    } catch {}
  }, [navOpen]);

  return (
    <html lang="he">
      <body className="min-h-screen bg-[#0F172A] text-slate-100">
        <div className="flex min-h-screen">
          <Nav navOpen={navOpen} setNavOpen={setNavOpen} />

          <div className="flex-1 min-w-0">
            <div className="sticky top-0 z-20 border-b border-slate-700/30 bg-[#0F172A]/85 backdrop-blur">
              <div className="flex items-center gap-3 px-4 py-2">
                <button
                  onClick={() => setNavOpen((v) => !v)}
                  className="rounded-md border border-gray-200 bg-white px-2 py-1 hover:bg-gray-50"
                  aria-label="Toggle navigation"
                  title="Toggle navigation"
                >
                  <div className="space-y-1">
                    <div className="h-0.5 w-5 bg-gray-800" />
                    <div className="h-0.5 w-5 bg-gray-800" />
                    <div className="h-0.5 w-5 bg-gray-800" />
                  </div>
                </button>

                <div className="text-sm font-semibold text-gray-800">
                  Robot Eco Farm
                </div>
              </div>
            </div>

            <main className="p-6">{children}</main>
          </div>
        </div>
      </body>
    </html>
  );
}
