"""Compress the embedded remote page for the Pico's small TCP send buffer."""
import gzip
from pathlib import Path

root = Path(__file__).resolve().parent
payload = gzip.compress((root / "index_v2.html").read_bytes(), compresslevel=9, mtime=0)
out = root / "build" / "webpage.h"
out.parent.mkdir(parents=True, exist_ok=True)
numbers = ",".join(str(b) for b in payload)
out.write_text("static const unsigned char webpage_gz[] = {" + numbers + "};\n"
               "static const unsigned webpage_gz_length = sizeof(webpage_gz);\n")
print("Generated compressed web page:", len(payload), "bytes")
