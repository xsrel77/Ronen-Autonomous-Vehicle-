"use client";

import Link from "next/link";
import { usePathname } from "next/navigation";

const items = [
  { href: "/dashboard", label: "Dashboard" },
  { href: "/data-analysis", label: "Data Analysis" },
  { href: "/control", label: "Control" },
  { href: "/missions", label: "Missions" },
  { href: "/logs", label: "Logs" },
];

function NavLink({ href, label, pathname, onClick }) {
  const active = pathname === href;

  return (
    <Link
      href={href}
      onClick={onClick}
      className={`block rounded-md px-3 py-2 text-sm transition ${
        active
          ? "bg-blue-50 text-blue-700 font-semibold"
          : "text-gray-700 hover:bg-gray-100 hover:text-gray-900"
      }`}
    >
      {label}
    </Link>
  );
}

export default function Nav({ navOpen, setNavOpen }) {
  const pathname = usePathname();

  function closeNav() {
    setNavOpen(false);
  }

  return (
    <>
      {navOpen && (
        <div
          className="fixed inset-0 z-30 bg-black/40 lg:hidden"
          onClick={() => setNavOpen(false)}
        />
      )}

      <aside
        className={`fixed left-0 top-0 z-40 h-full w-72 border-r bg-white shadow-xl transition-transform duration-200 lg:hidden ${
          navOpen ? "translate-x-0" : "-translate-x-full"
        }`}
      >
        <div className="flex items-center justify-between border-b p-4">
          <div className="font-bold text-gray-900">Robot Eco Farm</div>
          <button
            onClick={() => setNavOpen(false)}
            className="rounded-md border border-gray-200 px-2 py-1 hover:bg-gray-50"
            aria-label="Close navigation"
            title="Close"
          >
            ×
          </button>
        </div>

        <nav className="space-y-1 p-2">
          {items.map((item) => (
            <NavLink
              key={item.href}
              href={item.href}
              label={item.label}
              pathname={pathname}
              onClick={closeNav}
            />
          ))}
        </nav>

        <div className="border-t p-4 text-xs text-gray-500">
          Debug snapshot, map, and analytics
        </div>
      </aside>

      {navOpen ? (
        <aside className="hidden w-52 border-r bg-white lg:block">
          <div className="border-b p-4">
            <div className="font-bold text-gray-900">Robot Eco Farm</div>
            <div className="mt-1 text-xs text-gray-500">Navigation</div>
          </div>

          <nav className="space-y-1 p-2">
            {items.map((item) => (
              <NavLink
                key={item.href}
                href={item.href}
                label={item.label}
                pathname={pathname}
              />
            ))}
          </nav>
        </aside>
      ) : null}
    </>
  );
}
