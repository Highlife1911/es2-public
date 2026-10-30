"""The data every backend-driven real test of //shell shares.

A list constant, not a filegroup: the tests keep `$(rootpath tools/es2prep.py)`
style arguments on single files, and Bazel accepts such a location only for a
label listed directly in `data` (a filegroup hides its members from it). The
labels are plain strings, so they resolve against the BUILD file that uses them
(//shell).
"""

# The campaign tools (es2chain, es2prep, es2campaign, es2loadout, es2menu_backend),
# the C core, the in-vitro helpers and the reference CD.
BACKEND_DATA = [
    "tools/es2menu_backend.py",
    "tools/es2chain.py",
    "tools/es2prep.py",
    "tools/es2campaign.py",
    "tools/es2loadout.py",
    ":es2prep_core",
    "//tools/reference-game/shell-invitro:es2shellprep",
    "//tools/reference-game/shell-invitro:slots_oracle.py",
    "//tools/reference-game/shell-invitro:reference_pins.py",
    "@es2_data//:cd",
]
