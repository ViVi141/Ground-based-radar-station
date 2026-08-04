#!/usr/bin/env python3
"""Fit GBRS DetectionSnrDb / DemClutterScale from 1.log illumination SNRs."""

import math

# One peak (or worst in-window) bestSnrDb per mechanical revolution from 1.log.
REVS = [
    ("W1", 5.38),
    ("W2", 0.23),
    ("W3", -2.87),  # worst dwell in that pass
    ("W4", 9.83),
    ("W5", 12.03),
]


def pd(threshold_db, samples):
    hits = sum(1 for _, snr in samples if snr >= threshold_db)
    return hits / len(samples), hits


def main():
    print("=== Observed SNR per revolution (dB) ===")
    for name, snr in REVS:
        print("  %s: %+.2f dB" % (name, snr))

    print()
    print("=== DetectionSnrDb sweep (Pd over 5 revs) ===")
    print("%8s  %6s  %s  %s" % ("gate", "Pd", "hits", "note"))
    all_pass_gates = []
    gate = 12.0
    while gate >= -10.0:
        p, h = pd(gate, REVS)
        note = ""
        if h == 5:
            note = "ALL PASS"
            all_pass_gates.append(gate)
        elif h == 4:
            note = "4/5 (miss W3)"
        elif h == 3:
            note = "3/5 (miss W2/W3)"
        print("%8.1f  %5.0f%%  %d/5  %s" % (gate, p * 100.0, h, note))
        gate -= 0.5

    worst = min(snr for _, snr in REVS)
    print()
    print("Worst-rev SNR = %.2f dB" % worst)
    print("Highest gate that still clears all 5 revs = %.1f dB" % max(all_pass_gates))

    print()
    print("=== Margin from worst sample ===")
    for margin in (0, 1, 2, 3):
        g = worst - margin
        p, h = pd(g, REVS)
        print(
            "  margin %d dB -> DetectionSnrDb=%.2f  Pd=%.0f%% (%d/5)"
            % (margin, g, p * 100.0, h)
        )

    rec = math.floor((worst - 1.0) * 2.0) / 2.0
    print()
    print("=== Primary recommendation (no clutter model) ===")
    print(
        "DetectionSnrDb = %.1f  (worst %.2f - 1 dB, half-dB snap)"
        % (rec, worst)
    )
    print("Expected Pd on this log = %.0f%%" % (pd(rec, REVS)[0] * 100.0))

    print()
    print("=== Hypothetical DemClutterScale relief ===")
    print(
        "Model: snr' = snr * (1+CNR)/(1+a*CNR); a=new_scale/old_scale."
    )
    print("Current config DemClutterScale=0.25; a is relative to THAT.")
    for cnr_db in (0, 3, 6, 10, 15):
        cnr = 10.0 ** (cnr_db / 10.0)
        print("  assume clutter/thermal CNR = %d dB:" % cnr_db)
        for a in (1.0, 0.5, 0.25, 0.0):
            lifted = []
            for name, snr in REVS:
                snr_lin = 10.0 ** (snr / 10.0)
                new_lin = snr_lin * (1.0 + cnr) / (1.0 + a * cnr)
                lifted.append((name, 10.0 * math.log10(new_lin)))
            h3 = pd(3.0, lifted)[1]
            h0 = pd(0.0, lifted)[1]
            snrs = [s for _, s in lifted]
            print(
                "    a=%.2f (scale->%.3f): SNR[%.1f,%.1f]  Pd@3=%d/5  Pd@0=%d/5"
                % (
                    a,
                    0.25 * a,
                    min(snrs),
                    max(snrs),
                    h3,
                    h0,
                )
            )

    print()
    print("=== Recommended gameplay packs ===")
    print(
        "PACK_A  DetectionSnrDb=-4.0  DemClutterScale=0.25  "
        "-> 5/5 on measured SNRs, no clutter assumption"
    )
    print(
        "PACK_B  DetectionSnrDb=0.0   DemClutterScale=0.0   "
        "-> needs CNR>~6 dB to also clear W3 at gate 0; else still miss W3"
    )
    print(
        "PACK_C  DetectionSnrDb=-3.0  DemClutterScale=0.10  "
        "-> clears log; light clutter retained"
    )
    print(
        "Also keep EnableCfarGate=false, KeepUndetected=true "
        "(already set)."
    )


if __name__ == "__main__":
    main()
