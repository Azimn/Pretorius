/* affect_curve.c — V4 nonlinear emotional persistence (impl).
 *
 * Saturating fixed-point.  No floats.  No allocations.  Deterministic.
 */
#include "affect_curve.h"
#include "../schema/schema_state.h"

static int clampi(int v, int lo, int hi){
    return v < lo ? lo : (v > hi ? hi : v);
}

int16_t affect_decay(int16_t current, int16_t salience, int base_rate_per_mille){
    /* salience-weighted decay: high salience → slow decay.
     * decay_factor = base_rate × (1 - salience/1000)^2, in per-mille units.
     * current * (1000 - decay_factor) / 1000 */
    int abs_current = current < 0 ? -current : current;
    if (abs_current == 0) return 0;

    int inv_sal = 1000 - clampi(salience, 0, 1000);  /* 0..1000 */
    /* nonlinear: inv_sal squared keeps high-salience items sticky. */
    int sq = (inv_sal * inv_sal) / 1000;             /* 0..1000 */
    int decay = (clampi(base_rate_per_mille, 0, 1000) * sq) / 1000;
    int keep  = 1000 - decay;

    int next = (current * keep) / 1000;
    /* zero-bias: clamp near-zero to exact zero to avoid asymptotic drift */
    if (next > -2 && next < 2) next = 0;
    return (int16_t)clampi(next, -1000, 1000);
}

int16_t affect_decay_steps(int16_t current, int16_t salience,
                           int base_rate_per_mille, uint32_t ticks){
    int abs_current = current < 0 ? -current : current;
    if (abs_current == 0 || ticks == 0) return current;

    int inv_sal = 1000 - clampi(salience, 0, 1000);
    int rate = clampi(base_rate_per_mille, 0, 1000);
    if (rate == 0) return current;

    /* Closed-form, uncapped-but-bounded multi-tick decay.
     *
     * The one-tick affect_decay path intentionally uses integer per-mille
     * arithmetic. Repeating that for long absences either costs a loop over
     * every hour or freezes high-salience values when the per-tick integer
     * decay rounds to zero. This path keeps the same squared inverse-salience
     * shape, but accumulates elapsed time in a wider pressure term:
     *
     *   keep = SCALE / (SCALE + rate * ticks * inv_salience^2)
     *
     * High-salience memories therefore resist movement without becoming
     * immortal, and long gaps keep producing measurable differences without
     * iterating over months or years. */
    if (inv_sal == 0) inv_sal = 1;
    {
        const uint64_t scale = 1000000000ull;
        uint64_t sq = (uint64_t)inv_sal * (uint64_t)inv_sal; /* 1..1,000,000 */
        uint64_t pressure = (uint64_t)rate * (uint64_t)ticks * sq;
        uint64_t denom = scale + pressure;
        int64_t next = ((int64_t)current * (int64_t)scale) / (int64_t)denom;
        if (next > -2 && next < 2) next = 0;
        return (int16_t)clampi((int)next, -1000, 1000);
    }
}

int16_t affect_hysteresis_apply(int16_t current, int delta){
    /* same-sign deltas: full force at low magnitude, mild damping near saturation.
     * opposite-sign deltas: attenuated by current magnitude (resistance to reversal). */
    int abs_current = current < 0 ? -current : current;
    int same_sign   = (current >= 0 && delta >= 0) || (current <= 0 && delta <= 0);

    int effective;
    if (same_sign){
        /* damping factor: 1.0 at magnitude 0, ~0.3 at magnitude 1000 */
        int damp = 1000 - (abs_current * 700) / 1000;     /* 300..1000 */
        effective = (delta * damp) / 1000;
    } else {
        /* reversal resistance: scales with current magnitude */
        int resist = (abs_current * 600) / 1000;          /* 0..600 */
        int weakened = (delta * (1000 - resist)) / 1000;
        effective = weakened;
    }
    int next = current + effective;
    return (int16_t)clampi(next, -1000, 1000);
}

int affect_habituate(int raw_magnitude, int consecutive_hits, int gap_turns){
    if (gap_turns > 0){
        /* gap since last event → reset habituation */
        consecutive_hits = 0;
    }
    int eff = raw_magnitude;
    int n = consecutive_hits;
    while (n > 0 && eff > 1){
        eff = (eff + 1) / 2;   /* halve, rounding up */
        --n;
    }
    return clampi(eff, 0, 255);
}

int affect_trait_amplifier(int schema_slot, const TraitVec *traits){
    if (!traits) return 1000;
    int N = traits->N, A = traits->A, O = traits->O, C = traits->C, E = traits->E;
    int amp = 1000;

    switch (schema_slot){
    case SCHEMA_USER_HOSTILE:
        /* high neuroticism → sticky hostility belief */
        amp = 1000 + (N - 50) * 10;            /* ~500..1500 at N extremes */
        break;
    case SCHEMA_USER_TRUSTWORTHY:
        /* high agreeableness → trust is sticky */
        amp = 1000 + (A - 50) * 8;
        break;
    case SCHEMA_USER_DECEPTIVE:
        /* low openness OR high neuroticism → paranoid */
        amp = 1000 + (N - 50) * 6 + (50 - O) * 4;
        break;
    case SCHEMA_USER_INTIMATE:
        /* high extraversion + agreeableness → sticky intimacy */
        amp = 1000 + (E - 50) * 4 + (A - 50) * 4;
        break;
    case SCHEMA_SELF_DIGNITY:
        /* high conscientiousness → defends dignity */
        amp = 1000 + (C - 50) * 6;
        break;
    case SCHEMA_RELATIONSHIP_OWED:
    case SCHEMA_RELATIONSHIP_OWES:
        /* high conscientiousness → tracks debts */
        amp = 1000 + (C - 50) * 4;
        break;
    case SCHEMA_USER_COMPETENT:
        /* high openness → notices competence */
        amp = 1000 + (O - 50) * 3;
        break;
    default:
        break;
    }
    return clampi(amp, 200, 2000);
}
