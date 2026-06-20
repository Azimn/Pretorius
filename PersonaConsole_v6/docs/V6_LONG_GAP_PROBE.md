# V6 Long Gap Probe

Observation harness for long absence decay. The harness establishes a Pretorius/Kiki relationship in a temp copy, seeds relation dimensions, schema pressure, high-salience memories, and one confirmed learned-knowledge correction, then backdates the relation file to simulate long gaps.

This probe is template-only and uses no model, cloud, database, embeddings, or network access.

| Gap | Seconds | Disposition | Trust | Threat | Intimacy | Resentment | Mood | Exhaustion | Drive values | Schema trustworthy | Schema hostile | Schema intimate | Schema dignity | LK count | LK max confidence |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| established | 0 | 814 | 596 | 500 | 290 | 0 | -361 | 312 | 350/340/150/0/750/450/500/300 | 0 | 0 | 965 | 0 | 1 | 880 |
| 1 month | 2592000 | 755 | 596 | 500 | 290 | 0 | -229 | 322 | 650/715/550/250/750/450/500/300 | 0 | 0 | 0 | 0 | 1 | 880 |
| 6 months | 15724800 | 451 | 596 | 500 | 290 | 0 | -92 | 322 | 650/715/550/250/750/450/500/300 | 0 | 0 | 0 | 0 | 1 | 880 |
| 1 year | 31536000 | 85 | 596 | 500 | 290 | 0 | -54 | 322 | 650/715/550/250/750/450/500/300 | 0 | 0 | 0 | 0 | 1 | 880 |
| 3 years | 94608000 | 1 | 596 | 500 | 290 | 0 | -229 | 322 | 650/715/550/250/750/450/500/300 | 0 | 0 | 0 | 0 | 1 | 880 |

Interpretation:

- `Drive values` are the eight engine drives in order: recognition, stimulation, provocation, communion, autonomy, continuity, vindication, repose.
- Learned knowledge confidence is expected to remain stable across absence. Absence should decay availability/affect, not rewrite confirmed knowledge.
- One-year and three-year gaps should not collapse to identical schema or drive state after the long-gap decay fix.
- Current observation: schema pressure and drive perturbations settle by the one-month row, while relationship disposition continues to distinguish one year from three years. That may be acceptable for hot affect, but long-lived relationship posture should probably live in relation dimensions, open loops, milestones, and learned/episodic memory rather than raw schema heat.
- Future tuning candidate: add explicit half-life policy per schema slot if we want intimacy/hostility schemas to leave a residual after months without making every old wound permanent.

Harness:

```bash
node tests/continuity/long_gap_probe.js
```
