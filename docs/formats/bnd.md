# BND: opaque definition/tuning resources (reader unresolved)

**No original BND reader has been established.**
The results below are bounded observations, not a recovered grammar or a
claim that retail collision code loads these files. Reference binary and
archive hashes are recorded in [collision.md](collision.md) and [vol.md](vol.md).

## Confirmed data and executable observations

- There are 84 physical members, 83 in SIMVOL0 and one RESULTS override in
  SIMPATCH: 4,840 bytes total, sizes 6..394, all even. VER31 adds none.
- Every member has at least four bytes. Interpreting the first four as an
  unsigned little-endian value yields 770513047..825990872. A Unix-seconds
  interpretation gives dates in 1994..1996, but **timestamp is a hypothesis**.
  The prefix has not been demonstrated to be a tag, count or length.
- DBSIM has the BND extension pointer at `0x4a09f4` (literal `0x4a0a3a`).
  An exhaustive absolute-pointer scan found no reference to the pointer
  slot, unlike the adjacent DAT/DTS/GL/DMG/COL extension slots. The literal
  has only that table reference. VSHELL contains no case-insensitive `bnd`
  byte sequence. These negative searches do not exclude constructed names
  or indirect access; they are not proof that no loader exists.
- The auxiliary VER31 check has the same negative result: DBSIM's sole BND
  literal has one pointer-table reference, with no absolute reference to
  that slot; VSHELL has no BND substring. `bnd-deep.json` records these
  binaries' separate hashes and addresses; those addresses are not used
  for the VER95 reader claims above.
- `SIMINTEL.BND` offsets 4..39 match 36 consecutive executable bytes at
  DBSIM `0x49f4f4`. `0x45fab2` indexes that **compiled** dword table.
- `MECHDMG.BND` offsets 6..67 match 62 consecutive bytes at `0x499fe0`;
  `0x415385` selects that compiled table. `MECH.BND` offsets 366..373 match
  eight bytes at `0x49a058`; the slide-damage branch of `0x418f74` reads
  that table at `0x4194be`. The observed instructions load image globals,
  not a pointer returned by a demonstrated BND read.
- Other unique long matches include `CTL_ALRT.BND` offset 36, length 150,
  at `0x49e4fa`, and `PMFD.BND` offset 18, length 104, at `0x49cacc`.
  Matching byte ranges are reported only as offsets, lengths and addresses;
  no original payload or constants block is reproduced.
- The bounded unique-eight-byte-run search finds matches for 25 of the
  83 base members, covering 1,466 member bytes in aggregate. These are
  candidate correspondences, not a universal grammar or a serialization
  order. The patch member is excluded from this particular search.

**Hypothesis:** these files serialize per-module tuning variables used by a
development binder, while this retail executable compiles at least some of
the same defaults. They are not demonstrated per-vehicle geometry. Different
body sizes and discontinuous matches do not justify a universal array-of-words
schema. Field widths, count bindings, ignored fields, class registration,
runtime allocation and a complete loader chain remain **open**.

## Patch precedence and support boundary

**Confirmed:** with both archives registered, priority-byte sorting
`0x467f94` and first-match search `0x468064` choose SIMPATCH's
`bnd\results.bnd` over SIMVOL0's copy; see [collision.md](collision.md).
The two 28-byte physical payloads differ at offsets 0, 1, 16, 17 and 18;
the differences affect both the prefix candidate and opaque body.
`static-evidence.json` records their hashes. An actual request for that path is
**unconfirmed**; do not claim the patch changes retail behavior merely
because it wins a hypothetical lookup.

The library exposes a clearly named **inspection**, retaining the whole
input as an opaque offset/length span. It does not call acceptance of an
opaque byte span a successful original-reader parse. The CLI and independent
audit report all 84 as `opaque-only`, separately from the four proved
grammars. There is no invented BND count/index validation. Recovering a
complete BND layout under the original-reader requirement remains unfinished.

Evidence: `<es2-data>\logs\codex-formats-collision-20261007\bnd_probe.py`,
`bnd-search.json`, `inventory.json`, `verification.json`. The first script
compares data in memory and records match metadata, never extracted bytes.
All archive identities and member size/hash metadata remain in the reports.

Everything above comes from DBSIM, VSHELL and the CD archives.

## Observation that would settle the loader hypothesis

In an oracle run, watch the resource opener `0x4730e8`
and archive search `0x468064` for BND paths during initialization, mission
entry and results display. Log callers and read destinations. Watch writes
to `0x49f4f4`, `0x499fe0` and `0x49a058` after image mapping, then compare
their values before/after the suspected load. No BND opens in one run would
only establish that run's coverage; it would not prove removal of the loader.
An authoring/debug executable or a statically identified indirect binder
could supply the missing original grammar. For the timestamp hypothesis,
the decisive evidence would be an original reader/writer treating the prefix
as a time or version value; merely seeing date-shaped values is insufficient.
Field widths/count bindings and ignored fields require that reader's read
destinations and lengths.
