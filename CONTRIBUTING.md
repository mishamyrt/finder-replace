# Contributing

## Build and test

Requires macOS 13+ and Xcode Command Line Tools (`xcode-select --install`).
The program uses C11 and system frameworks only.

```sh
make
make test
./build/finder-replace --version
```

The executable is `build/finder-replace`; no `.app` bundle is created.
Use `make clean` to remove build output.

## Run locally

Configure an app and grant Accessibility access as described in the
[README](README.md). Stop any installed service before running a local build:

```sh
brew services stop finder-replace
./build/finder-replace --debug
```

Press **Ctrl+C** to stop. A rebuild may require granting Accessibility access
again. Diagnostics go to stderr.

Automated tests cover configuration, version output, display edges, AX retries
and deadlines, Finder identification, and mouse-event pairing. They do not test
live Dock clicks. After changing the hook, manually check the first click after
a pause, repeated clicks, screen-edge clicks, other Dock icons,
modified/right-clicks, and Dock hiding or magnification.

## Code overview

- `finder-replace.c`: configuration, event tap, AX hit-testing, and app activation.
- `test.c`: C regression checks; no Accessibility permission needed.
- `scripts/homebrew_formula.py`: generates the formula from release archives.
- `.github/workflows/qa.yml`: builds and tests on branch pushes and pull requests.
- `.github/workflows/release.yml`: builds tagged releases and updates the tap.

Keep the event callback bounded. AX requests have a 100 ms timeout within a
250 ms search budget, with at most six ancestors checked. Hit-testing retries
`kAXErrorCannotComplete` once; failures pass the click through.
When AX returns `kAXErrorNoValue` in the last point at a bottom/right display
edge, hit-testing probes eight points inside the edge to cross the observed
five-point gap below Dock's AX items. This fallback shares the search budget
and still requires a Dock item with Finder's URL. Successful original hits are
unchanged; shared edges use the containing display. Debug output includes both
coordinates without rounding them to integers.

Finder is identified by its Dock process, AX role, and application URL, not its
localized name. There is no polling or cached icon rectangle. A captured press
also consumes its drag and release. App activation runs outside the callback;
a launch failure is logged, and the consumed click is not replayed.

## Versioning and releases

`VERSION` in the Makefile is the source of truth. It is passed to the compiler as
`APP_VERSION`; `finder-replace --version` prints it. Use `make -s version` to read
it without building.

[QA](.github/workflows/qa.yml) builds and tests Apple Silicon (`arm64`) and Intel
(`x86_64`) binaries on branch pushes and pull requests. Pushing a tag matching
`vX.Y.Z` runs the separate [release workflow](.github/workflows/release.yml),
which builds, tests, signs, and publishes both binaries. Only stable `X.Y.Z` versions
are supported.

### One-time setup

Add the repository secret **`HOMEBREW_TAP_TOKEN`**: a fine-grained PAT for
`mishamyrt/homebrew-tap` with **Contents: Read and write**. The token and branch
rules must allow pushing to the tap's `main` branch. GitHub Releases use the
built-in `GITHUB_TOKEN`.

Release signing uses a self-signed code-signing certificate exported with its
private key from Keychain Access:

1. Open **Keychain Access → login → My Certificates** and find your code-signing
   certificate. Expand it to check that its private key is present.
2. Select the certificate and its private key, then choose **File → Export Items**.
   Use **Personal Information Exchange (.p12)** and save it outside the repository,
   for example as `~/Desktop/finder-replace-signing.p12`. A `.cer` export does not
   contain the private key and cannot sign binaries.
3. Set a strong export password and confirm access to the keychain when prompted.
   See Apple's [Keychain Access export instructions](https://support.apple.com/guide/keychain-access/kyca35961/mac).
4. In this repository's **Settings → Secrets and variables → Actions**, add these
   repository secrets:

   | Secret | Value |
   | --- | --- |
   | `FINDER_REPLACE_SIGNING_CERTIFICATE` | Base64-encoded contents of the `.p12` file |
   | `FINDER_REPLACE_SIGNING_CERTIFICATE_PASSWORD` | The export password from step 3 |

   Copy the certificate value on macOS with:

   ```sh
   base64 -i "$HOME/Desktop/finder-replace-signing.p12" | pbcopy
   ```

The workflow signs both architectures with identifier `co.myrt.finder-replace`,
disables timestamping, and verifies the signature before packaging. Keep using
the same certificate for subsequent releases. Store the `.p12` and its password
securely; do not commit them.

### Publish

1. Run the checks above and commit your code changes.
2. Update `VERSION` in the Makefile.
3. Publish:

   ```sh
   make publish
   ```

This stages and commits only the Makefile with the message
`chore: release vX.Y.Z`, creates tag `vX.Y.Z`, and atomically pushes the current
branch and tag to `origin`. Other staged files stay staged.

The workflow verifies the version and publishes two archives:
`finder-replace_vX.Y.Z_darwin_arm64.tar.gz` and
`finder-replace_vX.Y.Z_darwin_x86_64.tar.gz`, plus `SHA256SUMS`.
It then updates `Formula/finder-replace.rb` in `mishamyrt/homebrew-tap`, including
the service definition for `brew services`.

Reruns reuse existing release files and calculate formula checksums from those
files. If the tap update fails, fix access and rerun the failed job.
Release binaries are self-signed, not Developer ID signed or notarized.
