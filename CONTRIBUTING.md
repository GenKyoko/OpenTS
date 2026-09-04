# Contributing to OpenTS

OpenTS welcomes focused bug reports, proposals, documentation improvements, and
code contributions. Visual Studio 2022 Win32 Debug and Release builds are the
supported development target. A successful build is not runtime evidence, and
every contribution must distinguish the two.

## Before starting

- Search existing issues and pull requests.
- Open an issue before investing in a large feature, intentional behavior
  change, subsystem replacement, or compatibility break.
- Read [Building OpenTS](docs/BUILDING.md) and [Style](docs/STYLE.md) before
  changing source.
- Never commit game assets, original executables, proprietary SDKs,
  credentials, personal data, IDE state, or build output.

## Contribution workflow

Public contributions are pull-request first. Maintainers may commit directly
when appropriate. Pull requests are squash-merged, so keep the proposed change
focused and give it a summary that can become a clear project-history entry.

Separate mechanical cleanup from behavior changes. A formatting pass, rename,
or ownership refactor must not conceal a gameplay, format, persistence, or
network change.

Classify what a change does to externally visible behavior:

- **Behavior preserved:** an internal improvement that leaves every
  externally visible behavior the same. It needs no change record; state why
  the existing documentation remains accurate.
- **Bug fix:** the change corrects behavior that is defective for the
  project's goals, whether inherited or newly introduced.
- **Intentional change:** the change deliberately chooses a different
  outcome — a feature, a balance or performance change, or a removal.

A fix or intentional change that players or modders can see is documented by
a change record in the manual, categorized as a feature, fix, balance,
performance, or internal change, carrying its migration steps when it
breaks compatibility — see [Documentation](#documentation).

The TibSun reconstruction and original executable are historical evidence, not
automatic correctness or acceptance criteria for active OpenTS development.

## Compatibility boundaries

A compatibility boundary is anything outside the engine that depends on how
OpenTS behaves: mods, maps, and the documented configuration they rely on;
game-data formats and their defaults; saved games and replays; the packets a
network game exchanges and the deterministic simulation that keeps its
players in sync; and the COM interfaces and layout-sensitive structures
other code consumes. A change near a boundary can silently break someone's
mod, save, or network game, so such changes are made deliberately, never in
passing.

Before changing behavior at a boundary:

1. Establish what the engine does today, with evidence.
2. Work out who is affected: which versions, data, mods, saved state,
   network peers, or consumers.
3. Add focused tests or other reproducible evidence.
4. Update the owning documentation in the same change.
5. When the change is incompatible, give practical migration guidance.

Compatibility across versions is promised only where a documented contract
provides it. In particular, saves and network sessions carry the project
version, so different release versions refuse to load each other's saves or
play together. Development snapshots within one cycle share that version
stamp: they may accumulate incompatible save, replay, network, and
simulation changes before the release, and no interchange between snapshots
is promised. Test against the current snapshot, document what the change
means for the release, and open a new development version only through the
release lifecycle that [Maintaining](manual/MAINTAINING.md) describes.
[Build identity](docs/BUILDING.md) explains how the version stamp differs
from the diagnostic commit identity.

## Source changes

- Target C++20 for new and substantially rewritten C++ while modernizing
  inherited code incrementally.
- Shape new work so the engine's incremental migration toward an
  entity-component architecture stays possible;
  [Project direction](docs/DIRECTION.md) records the direction.
- Follow surrounding naming and layout; do not format unrelated code.
- Keep honest reconstruction placeholders until evidence supports a better
  name.
- Preserve historical file headers and all SPDX, copyright, modification, and
  GPL Section 7 notices.
- Correct an inaccurate ordinary historical comment narrowly when current code
  or stronger evidence proves it wrong.
- Use `//` or the established block form for ordinary prose. Reserve `///` for
  genuine XML documentation.

Do not remove or consolidate an uncertain file notice. Retain it and establish
the file's history before making a legal or attribution change. The controlling
terms are in [LICENSE.md](LICENSE.md).

## Documentation

Every contribution must account for its documentation impact. Changes to
behavior, interfaces, configuration, commands, scripting, compatibility,
architecture, build procedures, or contributor workflows must update their
owning documentation in the same contribution. A purely mechanical, test-only,
or internal refactor may need no prose change, but the contributor must state
why the existing documentation remains accurate.

Player- or modder-visible engine changes must update the
[OpenTS manual](manual/README.md) and the applicable lifecycle record in the
same contribution.

Give each fact one owner and link to it instead of copying it between guides.
Document current behavior, supported inputs, relevant limitations, and
migration requirements; do not turn plans or assumptions into current-state
claims.

For manual content, read [Authoring](manual/AUTHORING.md) and
[Manual style](manual/STYLE.md). Changes to manual tooling, schemas, generated
data contracts, lifecycle machinery, routes, or publication behavior also
require [Maintaining](manual/MAINTAINING.md).

## AI assistance

AI tools can help throughout the project: exploring the engine, drafting and
restructuring code and documentation, reverse engineering, and review.
Contributors are encouraged to use them where they help, and remain fully
responsible for the result: check every claim and behavior against current
source or observed evidence, apply project style, and review what is
submitted as their own work. AI output is not evidence — a build, a test, or
a runtime observation is — and it must not receive commit attribution.

## Validation

Run the narrowest relevant check first. Report exact commands, configurations,
environments, and results, plus material checks that were not run. A configured
project, a successful build, and a runtime observation are different results.

For source changes, build the affected supported configuration. Build both
Debug and Release when changing shared build configuration,
compiler-conditional code, or behavior that may differ under optimization.
Existing MSVC warnings remain; identify new warnings instead of treating the
current warning set as clean.

Behavior changes need focused, reproducible evidence. Automated checks must not
depend on proprietary game assets or original executables.

Continuous integration builds Win32 Debug and Release for every pull request
that touches the engine and is ready for review; a draft
pull request runs no checks until it is marked ready. It reports the same class
of result a local build does, so it does not replace the runtime evidence a
behavior change needs.

Continuous integration also requires the change record that
[Documentation](#documentation) calls for on every pull request that touches
`code/`. Mechanical work that no player or modder can observe — a refactor, a
formatting pass, a comment correction — is waived by the `no change record`
label, and applying the label re-runs the check.

## Pull request content

A pull request should include:

- a concise summary and rationale;
- the change classification and affected compatibility boundaries;
- exact validation results and material checks not run;
- documentation changes, or why none are needed;
- screenshots or recordings for visual changes when they add useful evidence.

Contributions are submitted under [the repository license](LICENSE.md),
including its applicable additional terms.
