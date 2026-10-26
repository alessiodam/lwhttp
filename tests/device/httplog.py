import os
import pathlib
import re
import sys

path = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "HTTPLOG.8xv")
if not path.exists():
    print(f"{path}: not found, the emulator did not save the log")
    sys.exit(1)

data = path.read_bytes()
length = int.from_bytes(data[0x48:0x4A], "little")
log = data[0x4A : 0x4A + length].decode("latin-1")
print(log)

summary = os.environ.get("GITHUB_STEP_SUMMARY")
if summary:
    with open(summary, "a", encoding="utf-8") as out:
        out.write("## HTTPTEST on CEmu\n\n```\n" + log + "\n```\n")

done = re.search(r"DONE pass (\d+) fail (\d+)", log)
if not done:
    print("HTTPTEST did not finish")
    sys.exit(1)
sys.exit(0 if done.group(2) == "0" and int(done.group(1)) > 0 else 1)
