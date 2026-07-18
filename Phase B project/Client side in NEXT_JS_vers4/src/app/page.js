import Link from "next/link";

export default function Home() {
  return (
    <div className="space-y-3">
      <h1 className="text-xl font-bold">Robot Eco Farm Client</h1>
      <Link className="underline" href="/dashboard">
        Go to Dashboard
      </Link>
    </div>
  );
}

