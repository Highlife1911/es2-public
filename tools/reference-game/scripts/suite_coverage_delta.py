"""Report newly observed original branch outcomes; never infer gameplay claims."""
import argparse
import json
from pathlib import Path


def inventory(summary):
    if summary.get("valid") is not True or not summary.get("module_sha256"):
        raise ValueError("coverage summary must be validated and identify its module")
    rows = {}
    for site in summary["sites"]:
        key = site["hook"], site["va"]
        if key in rows or any(type(site[d]) is not int or site[d] < 0 for d in ("fall", "taken")):
            raise ValueError("duplicate site or invalid counter")
        rows[key] = site
    return rows


def seen(site, outcome):
    """Observed, or known from the baseline of an edge-mode pass (known outcomes are not watched)."""
    return bool(site[outcome] or site.get(f"known_{outcome}"))


def delta(before, after):
    old, new = inventory(before), inventory(after)
    if before["module_sha256"] != after["module_sha256"]:
        raise ValueError("original binary identities differ")
    gained, closed, added = [], [], []
    for key, site in new.items():
        if key not in old:
            added.append(dict(hook=key[0], va=key[1], observed=[d for d in ("fall", "taken") if seen(site, d)]))
            continue
        prior = old[key]
        if prior["condition"] != site["condition"]:
            raise ValueError("condition changed for the same site")
        outcomes = [d for d in ("fall", "taken") if not seen(prior, d) and seen(site, d)]
        if outcomes:
            gained.append(dict(hook=key[0], va=key[1], outcomes=outcomes))
        if not (seen(prior, "fall") and seen(prior, "taken")) and all(seen(prior, d) or seen(site, d) for d in ("fall", "taken")):
            closed.append(dict(hook=key[0], va=key[1]))
    return dict(schema=1, valid=True, newly_observed_outcomes=gained, closed_site_gaps=closed,
                newly_instrumented_sites=added, unmeasured_prior_sites=[dict(hook=k[0],va=k[1]) for k in sorted(old.keys()-new.keys())],
                meaning="Union with baseline; site outcomes only. Named contact/slide/effect behavior still needs site/action review.")


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("baseline");p.add_argument("current");p.add_argument("--output",required=True)
    a=p.parse_args()
    result=delta(*[json.loads(Path(f).read_text(encoding="utf-8-sig")) for f in (a.baseline,a.current)])
    Path(a.output).write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
    print(f"Closed branch-site gaps (baseline union draft): {len(result['closed_site_gaps'])}")
    for row in result["newly_observed_outcomes"]:
        print(f"  {row['hook']} {row['va']}: new {', '.join(row['outcomes'])}")
    print(f"Newly instrumented sites (no historical comparison): {len(result['newly_instrumented_sites'])}")
    print(result["meaning"])


if __name__=="__main__":
    main()
