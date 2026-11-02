"""Keep the pre-layout admission reader as an independent test-only oracle."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
source = subprocess.check_output(
    ["git", "show", "6cc7513:port/native_geometry_formatter.h"], cwd=ROOT
).decode()
memory = source[source.index("struct GeometryFormatterMemory {"):source.index("inline bool geometry_formatter_overlap")]
helpers = source[source.index("inline bool open_geometry_formatter("):source.index("inline void close_geometry_formatter(")]
entry = source[source.index("struct GeometryFormatterEntry {"):source.index("struct GeometryFormatterWriteBounds")]
reader = source[source.index("inline bool read_geometry_formatter("):source.index("// The data callback consumes")]
out = ROOT / "analysis/native-geometry/phase3/format-layout-v1/formatter-admission-reference.inc"
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text("namespace legacy_formatter_admission {\nusing namespace buzz;\n" + memory + helpers + entry + reader + "}\n")
print("Pinned 6cc7513 test-only formatter admission ->", out)
