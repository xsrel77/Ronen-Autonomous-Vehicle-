export default function TelemetryPanel({ t }) {
  return (
    <div className="rounded-xl bg-blue-200 p-4 shadow-sm border">
      <div className="font-semibold mb-3">Telemetry</div>

      <div className="grid grid-cols-2 gap-3 text-sm">
        <div>
          <span className="text-gray-700 font-bold">Mode:</span> {t.mode}
        </div>
        <div>
          <span className="text-gray-700 font-bold">Battery:</span> {t.battery}%
        </div>
        <div>
          <span className="text-gray-700 font-bold">Speed:</span> {t.speed}
        </div>
        <div>
          <span className="text-gray-700 font-bold">Row:</span> {t.row}
        </div>

        <div className="col-span-2">
          <span className="text-gray-700 font-bold">Progress:</span>
          <div className="mt-1 h-2 w-full rounded  bg-gray-50">
            <div
              className="h-2 rounded bg-green-500"
              style={{ width: `${t.progress}%` }}
            />
          </div>
          <div className="mt-1 text-xs text-gray-700 font-bold">{t.progress}%</div>
        </div>

        <div>
          <span className="text-gray-700 font-bold">CPU:</span> {t.cpu ?? "—"}%
        </div>
        <div>
          <span className="text-gray-700 font-bold">Temp:</span> {t.temp ?? "—"}°C
        </div>

        <div className="col-span-2">
          <span className="text-gray-700 font-bold">Timestamp:</span>{" "}
          <span className="font-mono text-xs">{t.ts}</span>
        </div>
      </div>
    </div>
  );
}
