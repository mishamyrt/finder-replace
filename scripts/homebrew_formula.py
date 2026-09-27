"""Generate the tap formula from the actual release archives (stdlib only)."""

import argparse
import hashlib
from pathlib import Path
import re


def formula(version, repository, directory):
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
        raise ValueError("Expected a release version such as 0.1.0")
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository):
        raise ValueError("Expected an owner/repository name")
    archives = {}
    for arch in ("arm64", "x86_64"):
        name = f"finder-replace_v{version}_darwin_{arch}.tar.gz"
        archives[arch] = (name, hashlib.sha256((Path(directory) / name).read_bytes()).hexdigest())
    arm_name, arm_sha = archives["arm64"]
    intel_name, intel_sha = archives["x86_64"]
    base = f"https://github.com/{repository}"
    return f'''class FinderReplace < Formula
  desc "Open a configured application when clicking Finder in the Dock"
  homepage "{base}"
  version "{version}"

  depends_on macos: :ventura

  on_arm do
    url "{base}/releases/download/v{version}/{arm_name}"
    sha256 "{arm_sha}"
  end

  on_intel do
    url "{base}/releases/download/v{version}/{intel_name}"
    sha256 "{intel_sha}"
  end

  def install
    bin.install "finder-replace"
  end

  service do
    run [opt_bin/"finder-replace"]
    keep_alive crashed: true
    process_type :interactive
    log_path var/"log/finder-replace.log"
    error_log_path var/"log/finder-replace.log"
  end

  def caveats
    <<~EOS
      No clicks are intercepted until ApplicationPath is configured, for example:
        defaults write co.myrt.finder-replace ApplicationPath -string "/Applications/Bloom.app"
      Start at login with `brew services start finder-replace` (without sudo).
      After changing settings or granting Accessibility access, run:
        brew services restart finder-replace
      Background execution may need Accessibility permission for the binary itself:
        #{{opt_bin}}/finder-replace
      Stop with `brew services stop finder-replace` before running a foreground copy.
    EOS
  end

  test do
    assert_equal version.to_s, shell_output("#{{bin}}/finder-replace --version").strip
  end
end
'''


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("version")
    parser.add_argument("repository")
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    print(formula(args.version, args.repository, args.directory), end="")
