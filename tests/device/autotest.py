import argparse
import json
import os
import pathlib

parser = argparse.ArgumentParser()
parser.add_argument("--files", required=True)
parser.add_argument("--install-crc", required=True)
parser.add_argument("--verdict-crc", action="append", required=True)
parser.add_argument("--save-var", action="store_true")
parser.add_argument("--timeout", type=int, default=900000)
parser.add_argument("--out", required=True)
args = parser.parse_args()

out = pathlib.Path(args.out)
files = pathlib.Path(args.files)
transfer = ["bin/HTTPTEST.8xp"] + sorted(
    os.path.relpath(p, out.parent) for p in files.iterdir() if p.suffix.lower().startswith(".8x")
)

sequence = [
    "delay|5000",
    "key|clear",
    "action|launch:INSTALL",
    "hashWait|1",
    "key|left",
    "key|enter",
    "delay|1000",
    "action|launch",
    "hashWait|2",
]
if args.save_var:
    sequence += ["delay|2000", "saveVar|HTTPLOG"]
sequence.append("key|enter")

autotest = {
    "description": "LWHTTP HTTPTEST offline suite on CEmu",
    "transfer_files": transfer,
    "target": {"name": "HTTPTEST", "isASM": True},
    "sequence": sequence,
    "hashes": {
        "1": {
            "description": "lwIP-CE installed",
            "start": "vram_start",
            "size": "vram_16_size",
            "expected_CRCs": [args.install_crc],
            "timeout": 180000,
        },
        "2": {
            "description": "HTTPTEST offline suite passed",
            "start": "vram_start",
            "size": "vram_16_size",
            "expected_CRCs": args.verdict_crc,
            "timeout": args.timeout,
        },
    },
}

out.write_text(json.dumps(autotest, indent=2) + "\n")
