"""The known frame differences of the own process against DBSIM (own_process.md, "the frame compare as tests").

One row per suite and kind of difference of `dbsim_frames.py regions` (es2sim's frame capture against the
reference manifest REFERENCE below), with its cause. sim/tests/frames_suite.py fails on every difference that
has no row, on a row whose difference begins at another frame than `first` (earlier: a new difference;
later: an improvement, update the row), and on a row whose difference is gone (drop the row) - as
shell/tests/screen_known.c does for the screens. Rows with the cause "original varies" are the original's own
run-to-run differences: they never fail when gone, and their `exclude` frames leave the other-sites compare.

Fields: suite, kind (dbsim_frames.KINDS), first (the first differing frame, None for a row that only
excludes frames), cause (one of CAUSES; "unclassified" until the region capture places it), note, exclude
(frames; "original varies" only). Only frame numbers and kinds here, no hashes of the game's pixels beyond what
the manifests in es2-data hold.

REFERENCE is a capture of the original with region hashes (capture-dbsim-frames.ps1 -Regions). The heap layout
depends on the environment's copy, whose size is process input: replay_suite.py --frames runs es2sim with an
environment of REFERENCE_ENVIRONMENT's size; without it (a minimal or Bazel environment) c1_08-scan and
c1_08-escort-route differ, c1_08-scan from frame 1185.
"""

REFERENCE = "dbsim-frames-20261021-regions"
# The original's environment copy at CW3220's attach (es2-data logs\a1-heap-trace-20261023\original\heap-trace.ndjson:
# a pointer array of 420 bytes, then 102 string blocks of 5648 bytes; rover 0x02632438, 116 blocks at the entry):
# (strings, bytes of their heap blocks). es2sim with an environment of this size has the same rover offset and block
# count (...\es2sim-attach2-envpad). The size is the capture's input, not DBSIM's behaviour: CW3220 copies the
# process environment as it finds it, and es2sim copies its own the same way (rtl_imports.md, "the environment
# copy"), so only the frame tests pad.
REFERENCE_ENVIRONMENT = (102, 5648)

ORIGINAL_VARIES = "original varies"
# A difference that the whole-frame reference cannot place yet: the region capture (live request, own_process.md,
# "the frame compare as tests") names its region and with it the cause (the HUD, cockpit/panel/MFD, the present
# sites or the pump).
UNCLASSIFIED = "unclassified"
# A read past the end of a heap block whose bytes follow the heap layout, where it differs from the original's
# (own_process.md, "the free camera's shape edge"; only the environment's size differs, "the heap trace").
# No row has this cause.
HEAP_LAYOUT = "heap layout"
CAUSES = ("HD-13", "HD-14", "PR-03", "H5-20", HEAP_LAYOUT, UNCLASSIFIED, ORIGINAL_VARIES)

KNOWN = [
    # The original against itself, every hook original (presentation.md, "the original's own frame spread";
    # main_loop.md, "result of the frame-600 runs"). Presents of frame 204 / 600 per run, es2-data logs:
    # dbsim-frames-20261014 461 / 435, dbsim-frames-20261021 461 / 436, dbsim-frames-20261023-qpc 461 / 436,
    # dbsim-frames-20261021-regions 456 / 444, d-46-20261021 runs a/b/c 461/460/461 and 436/437/435. Between the
    # first three, through frame 859, only frame 600 differs (its set too); between them and -regions only the
    # present counts of 204 and 600. Each SOS-timer notice (0xa000) the dialog pump retrieves costs one modal
    # pass and one present; the timer posts in wall time, so the count per dialog frame follows the run's speed
    # (d-46: 11/12/11 notices at 204, 19/18/20 at 600, 84 in each run). No input of the run fixes it.
    {"suite": "c3_10-detail-options", "kind": "other_sites", "first": None, "cause": ORIGINAL_VARIES,
     "note": "the dialog presents of frames 204 and 600 vary between runs of the original", "exclude": [204, 600]},
]
