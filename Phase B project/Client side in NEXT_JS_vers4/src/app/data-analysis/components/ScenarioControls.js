import { SCENARIOS } from "../lib/scenarios";

export default function ScenarioControls({ scenarioId, setScenarioId }) {
  return (
    <section className="rounded-[2rem] border border-slate-800 bg-slate-900/65 p-4">
      <div className="text-[11px] font-semibold uppercase tracking-[0.3em] text-amber-300">
        Simulation scenario
      </div>

      <div className="mt-3 flex flex-wrap gap-2">
        {Object.values(SCENARIOS).map((scenario) => {
          const active = scenario.id === scenarioId;

          return (
            <button
              key={scenario.id}
              type="button"
              onClick={() => setScenarioId(scenario.id)}
              className={`rounded-full border px-4 py-2 text-xs font-semibold uppercase tracking-[0.18em] transition ${
                active
                  ? "border-amber-300 bg-amber-300 text-slate-950"
                  : "border-slate-700 bg-slate-950/70 text-slate-300 hover:border-slate-500"
              }`}
            >
              {scenario.label}
            </button>
          );
        })}
      </div>

      <p className="mt-3 text-sm leading-6 text-slate-400">
        {SCENARIOS[scenarioId]?.description}
      </p>
    </section>
  );
}