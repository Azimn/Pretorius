# V6 Long Gap Probe

Observation harness for long absence decay. The harness establishes a Pretorius/Kiki relationship in a temp copy, seeds relation dimensions, schema pressure, high-salience memories, one confirmed learned-knowledge correction, and one late single-event grievance spike, then backdates the relation file to simulate long gaps.

This probe is template-only and uses no model, cloud, database, embeddings, or network access.

| Gap | Seconds | Disposition | Trust | Threat | Intimacy | Resentment | Mood | Exhaustion | Drive values | Schema trustworthy | Schema hostile | Schema intimate | Schema dignity | LK count | LK max confidence |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| established | 0 | 774 | 583 | 527 | 290 | 27 | -605 | 325 | 600/355/270/0/830/450/800/300 | 0 | 714 | 958 | 0 | 1 | 880 |
| 1 month | 2592000 | 715 | 583 | 522 | 290 | 18 | -565 | 335 | 617/429/326/37/799/450/712/300 | 0 | 56 | 76 | 0 | 1 | 880 |
| 6 months | 15724800 | 411 | 583 | 511 | 290 | 7 | -245 | 335 | 638/562/439/127/767/450/585/300 | 0 | 9 | 12 | 0 | 1 | 880 |
| 1 year | 31536000 | 45 | 583 | 507 | 290 | 4 | -83 | 335 | 643/617/481/169/759/450/549/300 | 0 | 4 | 5 | 0 | 1 | 880 |
| 3 years | 94608000 | 1 | 583 | 503 | 290 | 0 | -419 | 335 | 648/675/523/216/753/450/518/300 | 0 | 0 | 0 | 0 | 1 | 880 |

Interpretation:

- `Drive values` are the eight engine drives in order: recognition, stimulation, provocation, communion, autonomy, continuity, vindication, repose.
- Learned knowledge confidence is expected to remain stable across absence. Absence should decay availability/affect, not rewrite confirmed knowledge.
- One-year and three-year gaps should not collapse to identical schema or drive state after the long-gap decay fix.
- Relation dimensions are intentionally asymmetric: trust/intimacy/admirational posture resists absence, while low-salience threat or resentment spikes soften toward their floor over month-scale gaps.
- Future tuning candidate: add explicit half-life policy per schema slot if we want intimacy/hostility schemas to leave a stronger residual after months without making every old wound permanent.

Harness:

```bash
node tests/continuity/long_gap_probe.js
```
