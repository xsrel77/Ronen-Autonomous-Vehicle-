"use client";

import { useMemo, useState } from "react";
import { buildRagAnswer, RESEARCH_PAPERS, searchResearchPapers } from "../lib/researchPapers";

const SUGGESTED_QUESTIONS = [
  "How does Kriging help predict unsampled tomato maturity?",
  "Which papers support the YOLO12M tomato layer?",
  "How does M5Stick data connect to microclimate?",
  "Why do we need LiDAR location for the ecological model?",
];

function SourceBadge({ paper }) {
  return (
    <span className="inline-flex rounded-full border border-slate-700 bg-slate-950/80 px-2.5 py-1 text-xs font-semibold text-slate-300">
      [{paper.id}] {paper.shortTitle}
    </span>
  );
}

function PaperCard({ paper }) {
  return (
    <article className="rounded-3xl border border-slate-800 bg-slate-950/70 p-4">
      <div className="flex flex-wrap items-start justify-between gap-3">
        <div>
          <div className="text-[10px] font-semibold uppercase tracking-[0.24em] text-emerald-300">Source [{paper.id}]</div>
          <h3 className="mt-2 text-base font-semibold text-white">{paper.shortTitle}</h3>
        </div>
        <span className="rounded-full border border-cyan-400/20 bg-cyan-400/10 px-3 py-1 text-[11px] font-semibold text-cyan-100">
          {paper.topic}
        </span>
      </div>

      <p className="mt-3 text-sm leading-6 text-slate-400">{paper.mainIdea}</p>
      <div className="mt-3 rounded-2xl border border-slate-800 bg-slate-900/70 p-3 text-sm leading-6 text-slate-300">
        <span className="font-semibold text-white">EcoFarm use:</span> {paper.relevance}
      </div>
      <details className="mt-3 text-xs leading-5 text-slate-500">
        <summary className="cursor-pointer text-slate-400 hover:text-white">Citation</summary>
        <p className="mt-2">{paper.citation}</p>
      </details>
    </article>
  );
}

export default function ResearchRagPanel() {
  const [search, setSearch] = useState("");
  const [question, setQuestion] = useState("How does Kriging help predict unsampled tomato maturity?");
  const [submittedQuestion, setSubmittedQuestion] = useState(question);

  const filteredPapers = useMemo(() => searchResearchPapers(search), [search]);
  const ragResult = useMemo(() => buildRagAnswer(submittedQuestion), [submittedQuestion]);

  function submitQuestion(event) {
    event.preventDefault();
    if (!question.trim()) return;
    setSubmittedQuestion(question.trim());
  }

  return (
    <section className="space-y-6 rounded-[2rem] border border-slate-800 bg-slate-900/65 p-5">
      <div className="grid gap-5 xl:grid-cols-[0.95fr_1.05fr]">
        <div>
          <div className="text-[11px] font-semibold uppercase tracking-[0.3em] text-emerald-300">Academic RAG prototype</div>
          <h2 className="mt-2 text-2xl font-semibold text-white">Research assistant for the EcoFarm model</h2>
          <p className="mt-2 text-sm leading-6 text-slate-400">
            This interface searches the five selected academic sources and gives a short grounded answer for the project. It is a local prototype RAG: the papers are represented by summaries, relevance notes, and citations inside the system.
          </p>

          <div className="mt-5 rounded-3xl border border-slate-800 bg-slate-950/70 p-4">
            <div className="text-sm font-semibold text-white">Search papers</div>
            <input
              value={search}
              onChange={(event) => setSearch(event.target.value)}
              placeholder="Search: Kriging, YOLO, LiDAR, microclimate, timeline..."
              className="mt-3 w-full rounded-2xl border border-slate-700 bg-slate-950 px-4 py-3 text-sm text-white outline-none transition placeholder:text-slate-600 focus:border-emerald-400/60"
            />
            <div className="mt-3 flex flex-wrap gap-2">
              {RESEARCH_PAPERS.map((paper) => (
                <SourceBadge key={paper.id} paper={paper} />
              ))}
            </div>
          </div>
        </div>

        <div className="rounded-3xl border border-slate-800 bg-slate-950/70 p-4">
          <div className="text-sm font-semibold text-white">Ask the research assistant</div>
          <form onSubmit={submitQuestion} className="mt-3 flex flex-col gap-3 md:flex-row">
            <input
              value={question}
              onChange={(event) => setQuestion(event.target.value)}
              className="min-w-0 flex-1 rounded-2xl border border-slate-700 bg-slate-950 px-4 py-3 text-sm text-white outline-none transition placeholder:text-slate-600 focus:border-cyan-400/60"
              placeholder="Ask about the papers and the EcoFarm model..."
            />
            <button className="rounded-2xl border border-cyan-400/30 bg-cyan-400/15 px-5 py-3 text-sm font-semibold text-cyan-100 transition hover:bg-cyan-400/25">
              Answer
            </button>
          </form>

          <div className="mt-3 flex flex-wrap gap-2">
            {SUGGESTED_QUESTIONS.map((item) => (
              <button
                key={item}
                onClick={() => {
                  setQuestion(item);
                  setSubmittedQuestion(item);
                }}
                className="rounded-full border border-slate-700 bg-slate-900 px-3 py-1.5 text-xs text-slate-300 transition hover:border-slate-500 hover:text-white"
              >
                {item}
              </button>
            ))}
          </div>

          <div className="mt-4 rounded-3xl border border-emerald-400/20 bg-emerald-400/10 p-4">
            <div className="text-[10px] font-semibold uppercase tracking-[0.24em] text-emerald-200">Question</div>
            <div className="mt-1 text-sm font-semibold text-white">{submittedQuestion}</div>
            <p className="mt-3 text-sm leading-6 text-emerald-50/90">{ragResult.answer}</p>
            <div className="mt-3 flex flex-wrap gap-2">
              {ragResult.sources.map((paper) => (
                <SourceBadge key={paper.id} paper={paper} />
              ))}
            </div>
          </div>
        </div>
      </div>

      <div className="grid gap-4 lg:grid-cols-2 xl:grid-cols-3">
        {filteredPapers.map((paper) => (
          <PaperCard key={paper.id} paper={paper} />
        ))}
      </div>
    </section>
  );
}
