#!/usr/bin/env python3
"""Offline regression harness for the GBRS multi-station datalink IFF.

Does NOT run the game. It mirrors, 1:1 in Python, the two pieces of logic changed
across this session so the intended rules can be checked without a Workbench:

  1. RDF_RadarFusionService.MergeIff  (RDF repo)
     Multi-station fusion must keep NEUTRAL distinct from UNKNOWN:
       same->same; UNKNOWN loses to any definite; NEUTRAL loses to FRIEND/FOE;
       FRIEND+FOE -> UNKNOWN.

  2. GBRS_RadarIffResolver (GBRS repo)
     Three-state IFF where the station's own side is its current camp-affiliated
     faction (Conflict follows camp occupation; GM falls back to prefab US/USSR):
       target faction == own        -> FRIEND
       target faction != own        -> FOE
       projectile / inbound         -> FOE
       no entity / unaffiliated     -> NEUTRAL

Run with:  python tools/verify_datalink_iff.py
Exits non-zero if any case fails, so it can be wired into CI.
"""

from __future__ import annotations

import sys

PASS = 0
FAIL = 0
CASES = []


# ---------------------------------------------------------------------------
# Enums (mirror the Enforce enums)
# ---------------------------------------------------------------------------
class Iff:
    UNKNOWN = 0
    FRIEND = 1
    FOE = 2
    NEUTRAL = 3


# ---------------------------------------------------------------------------
# 1) RDF_RadarFusionService.MergeIff
# ---------------------------------------------------------------------------
def merge_iff_old(a: int, b: int) -> int:
    if a == b:
        return a
    if a == Iff.UNKNOWN:
        return b
    if b == Iff.UNKNOWN:
        return a
    return Iff.UNKNOWN


def merge_iff_new(a: int, b: int) -> int:
    if a == b:
        return a
    if a == Iff.UNKNOWN:
        return b
    if b == Iff.UNKNOWN:
        return a
    if a == Iff.NEUTRAL:
        return b
    if b == Iff.NEUTRAL:
        return a
    return Iff.UNKNOWN


MERGE_TABLE = {
    "friend+friend": (Iff.FRIEND, Iff.FRIEND, Iff.FRIEND),
    "foe+foe": (Iff.FOE, Iff.FOE, Iff.FOE),
    "neutral+neutral": (Iff.NEUTRAL, Iff.NEUTRAL, Iff.NEUTRAL),
    "friend+unknown": (Iff.FRIEND, Iff.UNKNOWN, Iff.FRIEND),
    "foe+unknown": (Iff.FOE, Iff.UNKNOWN, Iff.FOE),
    "neutral+friend": (Iff.NEUTRAL, Iff.FRIEND, Iff.FRIEND),
    "neutral+foe": (Iff.NEUTRAL, Iff.FOE, Iff.FOE),
    "friend+foe": (Iff.FRIEND, Iff.FOE, Iff.UNKNOWN),
    "foe+friend": (Iff.FOE, Iff.FRIEND, Iff.UNKNOWN),
    "unknown+unknown": (Iff.UNKNOWN, Iff.UNKNOWN, Iff.UNKNOWN),
}


def check_merge():
    global PASS, FAIL
    print("== RDF_RadarFusionService.MergeIff ==")
    for name, (a, b, want) in MERGE_TABLE.items():
        got = merge_iff_new(a, b)
        ok = got == want
        PASS += ok
        FAIL += not ok
        tag = "PASS" if ok else "FAIL"
        # Show what the OLD logic returned, to prove the fix is meaningful.
        old = merge_iff_old(a, b)
        print(f"  [{tag}] {name:18s} new={iff_name(got)} want={iff_name(want)} old={iff_name(old)}")
        CASES.append((name, tag, got, want, old))


# ---------------------------------------------------------------------------
# 2) GBRS_RadarIffResolver
# ---------------------------------------------------------------------------
# A tiny stand-in for the FactionManager keys used by GetFactionKey().
FACTION_KEYS_ALL = {"US", "USSR", "GUE"}


class Faction:
    def __init__(self, key: str):
        self.key = key


def entity_faction(entity):
    """Mirror SCR_Faction.GetEntityFaction: entity's faction, or None."""
    if entity is None:
        return None
    return entity.get("faction")


def get_entity_faction_key(entity):
    f = entity_faction(entity)
    return f.key if f else None


class RadarIffResolver:
    """Mirror GBRS_RadarIffResolver (three-state, camp-affiliation own-side)."""

    def __init__(self, station: dict):
        self.station = station

    def resolve_own_faction_key(self):
        radar_subject = self.station.get("owner")
        # SCR_Faction.GetEntityFaction(owner): the camp-affiliated faction.
        if radar_subject is not None:
            ef = entity_faction(radar_subject)
            if ef is not None:
                return ef.key
            preset = self.station.get("preset")
            if preset == "USSR":
                return "USSR"
            return "US"
        return ""

    def resolve(self, track):
        own_key = self.resolve_own_faction_key()
        if not own_key:
            return Iff.UNKNOWN
        if track is None:
            return Iff.NEUTRAL
        if track.get("projectile"):
            return Iff.FOE
        entity = track.get("entity") or track.get("scatterer_entity")
        if entity is None:
            return Iff.NEUTRAL
        tf = entity_faction(entity)
        if tf is None:
            return Iff.NEUTRAL
        return Iff.FRIEND if tf.key == own_key else Iff.FOE


def gue_target(entity_faction_=Faction("GUE")):
    return {"entity": {"faction": entity_faction_}}


def civilian_target():
    return {"entity": {"faction": None}}


def us_target():
    return {"entity": {"faction": Faction("US")}}


def ussr_target():
    return {"entity": {"faction": Faction("USSR")}}


def shell_target():
    return {"projectile": True}


def no_entity_target():
    return {"entity": None, "scatterer_entity": None}


# station is: owner's current faction (camp-affiliated / default), plus preset.
# A GBRS station always has an owner entity; owner_faction_key=None models the
# owner being present but carrying no affiliation (GetEntityFaction -> null), so
# the resolver falls back to the GBRS preset.
def build_station(owner_faction_key=None, preset="US"):
    owner = {"faction": None}  # owner present, no affiliation by default
    if owner_faction_key is not None:
        owner = {"faction": Faction(owner_faction_key)}
    return {"owner": owner, "preset": preset}


def unpack_track(tr):
    return tr


IFF_SCENARIOS = [
    # name, station, track, want
    # GM: US station prefab always carries FactionAffiliationComponent default
    # "US", so GetEntityFaction(owner) -> US even though GM has no occupying camp.
    ("GM-US vs US", build_station("US", "US"), us_target(), Iff.FRIEND),
    ("GM-US vs USSR", build_station("US", "US"), ussr_target(), Iff.FOE),
    ("GM-US vs GUE", build_station("US", "US"), gue_target(), Iff.FOE),
    ("GM-US vs civilian", build_station("US", "US"), civilian_target(), Iff.NEUTRAL),
    ("GM-US vs no-entity", build_station("US", "US"), no_entity_target(), Iff.NEUTRAL),
    ("GM-US vs shell", build_station("US", "US"), shell_target(), Iff.FOE),
    # GM: station with NO affiliation component at all -> preset fallback US.
    ("GM-no-affil vs US", build_station(None, "US"), us_target(), Iff.FRIEND),
    ("GM-no-affil vs USSR", build_station(None, "US"), ussr_target(), Iff.FOE),
    # Conflict: US camp owned by USSR after seizure -> own = USSR.
    ("Seized-US-camp vs US", build_station("USSR", "US"), us_target(), Iff.FOE),
    ("Seized-US-camp vs USSR", build_station("USSR", "US"), ussr_target(), Iff.FRIEND),
    # Conflict: normal US camp.
    ("Conflict-US vs US", build_station("US", "US"), us_target(), Iff.FRIEND),
    ("Conflict-US vs USSR", build_station("US", "US"), ussr_target(), Iff.FOE),
]


def check_resolver():
    global PASS, FAIL
    print("\n== GBRS_RadarIffResolver (three-state, camp-affiliation) ==")
    for name, station, track, want in IFF_SCENARIOS:
        got = RadarIffResolver(station).resolve(track)
        ok = got == want
        PASS += ok
        FAIL += not ok
        tag = "PASS" if ok else "FAIL"
        print(f"  [{tag}] {name:22s} -> {iff_name(got)} (want {iff_name(want)})")
        CASES.append((name, tag, got, want, None))


def iff_name(v: int) -> str:
    return {Iff.UNKNOWN: "UNKNOWN", Iff.FRIEND: "FRIEND", Iff.FOE: "FOE", Iff.NEUTRAL: "NEUTRAL"}[v]


def main():
    check_merge()
    check_resolver()
    print("\n================================================")
    print(f"RESULT: {PASS} passed, {FAIL} failed")
    if FAIL:
        print("FAILURES:")
        for name, tag, got, want, _ in CASES:
            if tag == "FAIL":
                print(f"  - {name}: got {iff_name(got)} want {iff_name(want)}")
        sys.exit(1)
    print("ALL PASS")


if __name__ == "__main__":
    main()
