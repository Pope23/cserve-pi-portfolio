# Release readiness

Assessment date: 2026-11-09. Scope: repository preparation for an educational
GitHub portfolio and interview demonstration, with no application behavior change.
No repository was created remotely, no push was performed, and no public listener
was started during verification.

## Local commits and intended changes

- Stage 7 finalized in `b2236ec`, `docs: finalize refactoring and security review`.
  Only README, design, the stage 7 report, and the five audit tests were included.
  Clean Clang/GCC runs each passed 41 Python methods and 26 C scenario groups first.
- The subsequent release-preparation change contains the English README, MIT
  license, CI workflow, this report, and metadata-only redactions in seven audit
  reports. Application sources, tests, Makefile, and dashboard are unchanged from
  the stage 7 commit.

Preparation sequence: inspect and finalize stage 7; verify official CI action
metadata and license/provenance; prepare CI and public documentation; validate
YAML and execute its commands from clean sources; inspect privacy/artifacts;
perform independent review. A subsequent local commit is conditional on these
checks and contains only the intended release files.

## CI configuration

[ci.yml](../.github/workflows/ci.yml) uses GitHub-hosted `ubuntu-24.04`, with:

- push and pull_request triggers restricted to the `main` branch;
- `contents: read`, with other token permissions omitted;
- separate GCC/Clang Release matrix entries, each running the real Make targets;
- a separate Clang AddressSanitizer/UndefinedBehaviorSanitizer job that instruments
  the server and all four C harnesses, with failures treated as failures;
- a ten-minute job timeout and no public self-hosted Raspberry Pi runner;
- checkout credentials not persisted and no credentials or secret values in YAML.

The official checkout release was checked against
[actions/checkout v7.0.1](https://github.com/actions/checkout/releases/tag/v7.0.1).
The workflow pins its full official commit
`3d3c42e5aac5ba805825da76410c181273ba90b1`, with the version in a comment.
The [Ubuntu runner image inventory](https://github.com/actions/runner-images/blob/main/images/ubuntu/Ubuntu2404-Readme.md)
was checked for the toolchain, so no package installation step or application
dependency was introduced.

PyYAML 6.0.2, already installed locally, parsed the file. Assertions checked its
event filters, runners, matrix, permission mapping, checkout pin, credential
setting, and sanitizer C17 flags. Bash executed the workflow's extracted `run`
commands locally. `actionlint` was not available and was not installed; YAML
parsing is not a complete GitHub Actions service validation.

**The hosted Ubuntu jobs have not run.** The verification host is Raspberry Pi 5
ARM64, not a GitHub-hosted x64 Ubuntu VM. A successful first remote CI run remains
a publication follow-up. No CI badge or hosted result is claimed in README.

## Build and test evidence

A new temporary source snapshot was created from `git archive HEAD` plus an
explicit overlay of the intended release files. It initially contained no `.git`,
build directory, backups, or Python caches. The following were run from its root:

| Check | Actual result |
| --- | --- |
| Workflow Release commands, GCC | Exit 0; 41/41 Python methods and 26/26 C groups. |
| Workflow Release commands, Clang | Exit 0; 41/41 Python methods and 26/26 C groups. |
| `make CC=clang debug && make CC=clang test` | Exit 0; 41/41 Python methods and 26/26 C groups. Server instrumented; later harness builds use normal flags. |
| Exact workflow Clang ASan/UBSan command and environment | Exit 0; 41/41 Python methods and 26/26 C groups, with all binaries instrumented. |
| `git diff --check`, new-file whitespace and YAML checks | Passed. |

Release uses the existing C17 `-O2` Makefile flags. Debug adds debug symbols,
`-O0`, ASan/UBSan and frame pointers. Full sanitizer validation uses the existing
`-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer` configuration and
strict warnings, with both sanitizers set to halt on errors. No compiler warning
or sanitizer diagnostic was observed in these runs.

The four C harnesses contain 8 HTTP response, 6 storage, 6 parser and 6 server
lifecycle groups. Python subtests and individual assertions are not counted as
extra methods. Expected injected errno messages are not test failures. The
normal SIGTERM shutdown used by integration tests does not establish the absence
of all leaks; the exit-time LSan limitation remains documented in the audit.

Logs and temporary checkouts remain outside the repository. No generated artifact
is intended for the release commit. These checks establish that builds do not
depend on existing local build products; they do not establish reproducible
binary hashes or performance benchmarks.

## Browser inspection

Chromium 147 was available. A loopback-only server was started and a sample task
was created through the real API for a headless DOM/screenshot attempt. That
attempt timed out without a screenshot. A minimal about:blank diagnostic returned
success, but logged EGL/display initialization errors. A second dashboard attempt
with GPU disabled also timed out. No successful rendered-DOM or screenshot check
is claimed; the precise cause was not established and the application was not
modified to accommodate the browser.

The HTTP dashboard test passes, source inspection confirms task text uses
textContent, and the page is byte-identical to the tested application version.
Interactive form use, layout, accessibility and browser-rendered escaping still
require a manual browser check on the device or another local environment.

## License and third-party provenance

[LICENSE](../LICENSE) contains the standard MIT text, using the copyright
pseudonym **Pope23** explicitly supplied for this preparation. The year is 2026;
no legal name or identity was invented. The canonical text was checked against
the [Open Source Initiative MIT license](https://opensource.org/license/mit).

The source tree, headers, HTML/JS, tests, build files and available local history
were inspected for existing license/copyright notices, vendored code, external
assets and library dependencies. No bundled third-party implementation, fonts,
images, JavaScript packages or C libraries were identified. Runtime/build/test
requirements are system tools, POSIX/system libraries and Python standard library;
their own licenses remain applicable to those components. The official checkout
action is referenced by the workflow rather than copied into the project.

This is a bounded provenance inspection, not proof of authorship or a legal
clearance of code origins outside the available repository. If externally sourced
snippets or assets were omitted from that history, the copyright owner must
identify them and preserve their required notices before publication.

## Privacy, generated files and Git history

- The English README contains no workstation username, personal filesystem path,
  private LAN address, installation-chat history, or unverified benchmark claim.
  Loopback URLs and the all-interface bind address are intentional API examples.
- Seven historical audit documents had workstation home paths and machine names
  generalized in the current tree. Results and commit IDs were preserved.
- Current tracked files and explicit new release files were scanned for selected
  private-key, AWS, GitHub-token and Slack-token patterns, personal home paths and
  RFC1918 addresses. No matches were found. This pattern scan is not a guarantee
  that every possible secret format has been detected.
- The same selected credential patterns were checked across all available local
  commits: no matches. Previous home-directory paths remain in seven historical
  file paths. Existing commit-author email metadata is also retained.
- **History was not rewritten.** Anyone publishing all current history should
  decide whether those historic environment identifiers and author attribution
  are acceptable. Current-file redaction does not remove earlier blobs.
- The tracked file inventory contains no build products, backups, credentials
  directories, Python bytecode, screenshots or logs. .gitignore excludes build,
  backups, Python caches, .env, editor artifacts and logs. Existing backups remain
  local and ignored. Additional private configuration should be reviewed before
  adding it; ignore rules do not protect already tracked files.

## Readiness and remaining limits

Ready as a locally verified educational source project, subject to the remaining
publication choices and checks below. It is not a production service, a complete
HTTP implementation, or suitable for direct public Internet exposure.

Before public release:

1. Review the retained history/author metadata and confirm any third-party code
   origins not represented in the repository. No secret/history rewrite was done.
2. Run the first GitHub-hosted workflow after explicitly creating the remote and
   publishing; inspect results rather than treating local ARM64 runs as Ubuntu CI.
3. Complete a manual browser session. The attempted visual check is inconclusive.

Known technical limits remain: memory-only state, sequential connections, no
auth/TLS, per-call inactivity timeouts, ignored socket-option and transport errors,
partial HTTP/UTF-8 validation, frontend character-vs-byte length mismatch, and
non-graceful signal termination. No behavior change was introduced for release.

## Publication commands — not executed

After the privacy/provenance review, create an empty GitHub repository manually.
Replace the placeholder with its actual URL; no remote currently exists here.

```sh
git status
git remote add origin <repository-url>
git push -u origin HEAD:main
```

The commands are instructions for a later, explicit publication action, not an
action performed during this preparation. No public Raspberry Pi runner or
Internet-facing server is required.

## Independent review

An independent read-only reviewer checked the workflow event/permission mapping,
Makefile compatibility, metadata-only report changes, whitespace and selected
current-file privacy/credential patterns. No critical or important issue was
found. Two minor wording issues were corrected: sanitizer jobs are independent,
not sequenced after the compiler matrix; the commit step is described as
conditional rather than prematurely complete. The reviewer did not run the
hosted workflow or independently rerun the complete application test suite.
