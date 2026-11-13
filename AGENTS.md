# Project instructions

- Read `docs/rust-roadmap.md` before starting a milestone, after completing one,
  and when resuming after context compaction. It is the short source of truth
  for scope, constraints, current checkpoint and next work.
- Re-read its delivery order and constraints at every completed increment,
  including a module-sized increment inside an unfinished milestone. Resume
  from its pending slice; update it before changing scope or order.
- At each milestone, update that roadmap with actual verification, commit and
  next task. Keep it concise; put detailed design/evidence in separate docs.
- Before continuing, check the worktree against the checkpoint. Keep verified,
  uncommitted and incomplete work distinct; do not infer completion from context
  summaries or from a helper module passing tests.
- Keep one current checkpoint in the roadmap, not an accumulating work log.
  A stage is complete only when its runnable exit condition is met.
- Develop in small runnable increments. Preserve the C reference; Rust browser
  builds currently go to `build/rust/site`.
- Local milestone commits are authorized. Do not push unless asked.
- Subagents, if delegation is explicitly authorized, must use an explicit model
  no more expensive than `gpt-6.1-sol` and effort no higher than `high`; pass this
  restriction to nested agents. Do not automatically revise this ceiling.
