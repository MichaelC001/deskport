# PC documentation audit verification — 2026-10-17

Application baseline: `b0c28182b8048497e32eadbae3561147d2c1969f`.
Audit checkout: `be315a1a788b21defb782104e3009be7094b43b1`.
Core: `e1850b07c072f283669a64ec8f312ffc73ba1219`.
The audit changes documentation/CI only; the Linux package source filter excludes
both directories. No installed application or service was replaced or restarted.

## Observed results

| Check | Environment and result |
| --- | --- |
| Evidence checker | macOS and native Linux; 12 invalid provenance fixtures rejected, historical CI SHAs retained across a snapshot change; 11 records and 3 CI jobs validated before appending this checkpoint. |
| Full Nix package | Native NixOS x86_64; `nix build --no-link` with a scheduling-only builder hook passed. Four live compiler processes independently reported CPU allowance `0-3` and nice `15`. Maximum one derivation / four build cores. |
| Package identity | Version/help, desktop entry, independent application ID and installed icon passed with synthetic state. The stock smoke script's Seamless self-test was deliberately not invoked. |
| Initial CLI fixture | Failed before the foreground owner became ready. Its long `TMPDIR` produced a 131-byte instance socket path; a separate synthetic Unix-socket bind confirmed `AF_UNIX path too long`. No later product assertions ran in that attempt. |
| Short-path CLI fixture | The same binary passed with a short, private `TMPDIR` under the project build root: no-display CLI, loopback HTTPS enrollment/list/removal/revoked credential rejection, isolated config/ownership/stale approval and SIGTERM cleanup. |

Nix output: `/nix/store/cclirqg1svs2qqaz4r75cavm0z4g98n0-deskport-0.7.2`.
Its **uncompressed output NAR** SHA-256 is
`4acdb72971703c2f05e5f752681c968714653fba72d92148a2c1c7a9f8b2814d`
(`sha256-Ss23KXFwPC8F5fdSaByWhxRlP7py2SFIosHHqfiygU0=`), size 6,959,688 bytes.
This identifies that output, not the complete dependency closure or a release
installer. It was not installed or activated.

Build logs, source snapshot, compiler-resource samples, socket probe and CLI logs
are retained privately under the checkout's build root. The first application
build attempt was interrupted when the daemon's compiler processes did not
inherit client-only CPU/nice limits; it is not counted as a passing build. The
successful hook applies the limits inside the builder before unpack/compilation.

These checks establish package/CLI behavior in isolated state. They do not
establish GPU capture/encoding, real viewer video/input, IME, audio, clipboard,
HDR, physical display restoration, Windows/macOS native acceptance or Seamless.

## Reproducing the resource restriction

Reserve the build host's established heavy-build lock and inspect its owner and
processes first. Use an isolated checkout of the recorded source. A client-only
`taskset`/`nice` does not constrain a Nix daemon's separate builder.

```sh
checkout_build=$(bash scripts/build-paths.sh checkout)
mkdir -p "$checkout_build/pc-support-verification"
cat > "$checkout_build/pc-support-verification/resource-limits.nix" <<'NIX'
let
  flake = builtins.getFlake ("path:" + builtins.getEnv "DESKPORT_CHECKOUT");
  pkgs = import flake.inputs.nixpkgs { system = "x86_64-linux"; };
in flake.packages.x86_64-linux.default.overrideAttrs (old: {
  nativeBuildInputs = (old.nativeBuildInputs or []) ++ [ pkgs.util-linux ];
  preUnpack = ''
    ${pkgs.util-linux}/bin/taskset -pc 0-3 "$$"
    ${pkgs.util-linux}/bin/renice -n 15 -p "$$"
    echo 'DeskPort task builder: CPU 0-3 and nice 15 applied before unpack/compilation'
  '' + (old.preUnpack or "");
})
NIX
DESKPORT_CHECKOUT="$PWD" taskset -c 0-3 nice -n 15 nix build --no-link \
  --max-jobs 1 --cores 4 --impure --print-out-paths \
  --file "$checkout_build/pc-support-verification/resource-limits.nix"
```

Use the resulting store path as `PACKAGE` below. Keep the CLI runtime prefix
short enough for Unix socket names; all fixture data still stays under the shared
project build root. Never shorten a path by reusing the user's configuration.

```sh
short_tmp=$(mktemp -d "$(bash scripts/build-paths.sh root)/p17.XXXXXX")
trap 'rmdir "$short_tmp"' EXIT
TMPDIR="$short_tmp" taskset -c 0-3 nice -n 15 \
  python3 scripts/test-host-cli.py "$PACKAGE/bin/deskport"
```

The fixture supplies its own HOME/XDG directories, certificates and loopback
ports, disables sharing and never injects remote input. This workaround verifies
the ordinary short-runtime-path case; long `TMPDIR` support is not accepted by it.
