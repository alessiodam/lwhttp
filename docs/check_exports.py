import pathlib
import re
import sys

docs = pathlib.Path(__file__).resolve().parent
root = docs.parent
exports = {
    line.strip()
    for line in (root / "exports.txt").read_text().splitlines()
    if line.strip() and not line.startswith("#")
}
directive = re.compile(r"^(?:[`:]{3,}\{c:autofunction\}|\.\. c:autofunction::)\s+(\w+)", re.M)
referenced = set()
for page in list(docs.rglob("*.md")) + list(docs.rglob("*.rst")):
    referenced.update(directive.findall(page.read_text()))

header = (root / "lwhttp.h").read_text()
commented = set()
for match in re.finditer(r"\*/\s*\n([^;{]*?\b(lwhttp_\w+)\s*\()", header):
    commented.add(match.group(2))

problems = []
problems += [f"export not in the API reference: {name}" for name in sorted(exports - referenced)]
problems += [f"API reference names a non-export: {name}" for name in sorted(referenced - exports)]
problems += [f"export has no doc comment in lwhttp.h: {name}" for name in sorted(exports - commented)]
for line in problems:
    print(line)
print(f"{len(exports & referenced & commented)} of {len(exports)} exports documented")
sys.exit(1 if problems else 0)
