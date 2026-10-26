import os
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
IDENT = r"(?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*)"
SEMVER = re.compile(
    r"(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)"
    rf"(?:-({IDENT}(?:\.{IDENT})*))?"
    r"(?:\+([0-9a-zA-Z-]+(?:\.[0-9a-zA-Z-]+)*))?"
)


def parse(version):
    m = SEMVER.fullmatch(version)
    if not m:
        return None
    pre = tuple(m.group(4).split(".")) if m.group(4) else ()
    return (int(m.group(1)), int(m.group(2)), int(m.group(3))), pre


def key(parsed):
    core, pre = parsed
    if not pre:
        return core, (1,)
    ids = tuple((0, int(p), "") if p.isdigit() else (1, 0, p) for p in pre)
    return core, (0,) + ids


def header_version():
    text = (ROOT / "lwhttp.h").read_text()
    macro = {n: re.search(rf"#define LWHTTP_VERSION_{n} (\S+)", text).group(1) for n in ("MAJOR", "MINOR", "PATCH")}
    string = re.search(r'#define LWHTTP_VERSION_STRING "([^"]+)"', text).group(1)
    parsed = parse(string)
    if not parsed:
        fail(f"LWHTTP_VERSION_STRING {string} is not valid SemVer")
    if parsed[0] != tuple(int(macro[n]) for n in ("MAJOR", "MINOR", "PATCH")):
        fail(f"LWHTTP_VERSION_STRING {string} disagrees with the MAJOR/MINOR/PATCH macros")
    return string, parsed


def fail(message):
    print(f"::error::{message}")
    sys.exit(1)


def git(*args):
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True, check=True).stdout


def exports(ref):
    if ref is None:
        text = (ROOT / "exports.txt").read_text()
    else:
        text = git("show", f"{ref}:exports.txt")
    return [line.strip() for line in text.splitlines() if line.strip() and not line.startswith("#")]


def bump_needed(old, new):
    if new[: len(old)] != old:
        return "major"
    if len(new) > len(old):
        return "minor"
    return "patch"


def check_release(tag):
    if not tag.startswith("v"):
        fail(f"tag {tag} must start with v")
    version = tag[1:]
    parsed = parse(version)
    if not parsed:
        fail(f"tag {tag} is not valid SemVer 2.0.0")
    string, _ = header_version()
    if string != version:
        fail(f"tag {tag} does not match LWHTTP_VERSION_STRING {string}")
    previous = []
    for name in git("tag", "--list", "v*").split():
        p = parse(name[1:])
        if p and name != tag:
            previous.append((key(p), name, p))
    older = [t for t in previous if t[0] < key(parsed)]
    newer = [t for t in previous if t[0] >= key(parsed)]
    if newer:
        fail(f"{tag} is not higher than existing release {max(newer)[1]}")
    if older:
        _, base, base_parsed = max(older)
        need = bump_needed(exports(base), exports(None))
        core, base_core = parsed[0], base_parsed[0]
        same_unstable = base_parsed[1] and base_core == core
        if base_core[0] == 0 or same_unstable:
            pass
        elif need == "major" and core[0] <= base_core[0]:
            fail(f"exports.txt removes or reorders exports of {base}; that needs a MAJOR bump")
        elif need == "minor" and core[:2] <= base_core[:2]:
            fail(f"exports.txt adds exports since {base}; that needs a MINOR or MAJOR bump")
        print(f"{tag}: valid SemVer, follows {base}, ABI change since then needs a {need} bump")
    else:
        print(f"{tag}: valid SemVer, first release")
    prerelease = "true" if parsed[1] else "false"
    out = os.environ.get("GITHUB_OUTPUT")
    if out:
        with open(out, "a") as f:
            f.write(f"version={version}\nprerelease={prerelease}\n")


def self_test():
    for good in ["1.0.0", "1.0.0-rc1", "1.0.0-rc.1", "1.0.0-alpha.beta.1", "1.0.0+build.5", "0.1.0-0.3.7"]:
        assert parse(good), good
    for bad in ["1.0", "01.0.0", "1.0.0-", "1.0.0-01", "1.0.0+", "v1.0.0", "1.0.0-rc..1"]:
        assert not parse(bad), bad
    order = ["1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta", "1.0.0-beta.2", "1.0.0-beta.11",
             "1.0.0-rc.1", "1.0.0", "1.0.1", "1.1.0", "2.0.0"]
    keys = [key(parse(v)) for v in order]
    assert keys == sorted(keys) and len(set(keys)) == len(keys)
    assert bump_needed(["a", "b"], ["a", "b"]) == "patch"
    assert bump_needed(["a", "b"], ["a", "b", "c"]) == "minor"
    assert bump_needed(["a", "b"], ["a"]) == "major"
    assert bump_needed(["a", "b"], ["b", "a"]) == "major"
    print("semver self-test passed")


if __name__ == "__main__":
    if sys.argv[1:] == ["--self-test"]:
        self_test()
    elif sys.argv[1:] == ["--check-header"]:
        print(f"LWHTTP_VERSION_STRING {header_version()[0]} is valid SemVer")
    elif len(sys.argv) == 2:
        check_release(sys.argv[1])
    else:
        fail("usage: semver.py --self-test | --check-header | vX.Y.Z[-pre][+build]")
