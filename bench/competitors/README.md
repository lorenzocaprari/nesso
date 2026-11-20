# DIY retrieval baselines

Optional competitor engines for `scripts/eval-retrieval`. They are not installed
in the build image, Pipeline, or the nightly `eval-retrieval` workflow.

The pin file is named `diy-faiss.hashes.txt` (not `*requirements*.txt`) so GitHub's
dependency graph / PR dependency-review does not index this optional stack.

## diy-faiss

sentence-transformers `all-MiniLM-L6-v2` (fp32, pinned Hub revision) + FAISS
`IndexFlatIP`. Setup and fairness notes: [docs/benchmarks.md](../../docs/benchmarks.md).

```bash
python3.12 -m venv .venv-diy
.venv-diy/bin/pip install --require-hashes \
  -r bench/competitors/diy-faiss.hashes.txt \
  --extra-index-url https://download.pytorch.org/whl/cpu
PYTHONHASHSEED=0 .venv-diy/bin/python scripts/eval-retrieval \
  --dataset all-2k --engine diy-faiss --out build/eval/diy_faiss.json
```

Regenerate the lockfile (Python 3.12):

```bash
uv pip compile --python 3.12 --generate-hashes \
  --extra-index-url https://download.pytorch.org/whl/cpu \
  --index-strategy unsafe-best-match \
  bench/competitors/diy-faiss.in -o bench/competitors/diy-faiss.hashes.txt
```

Local vuln check (dependency-review will not see this file):

```bash
python3 - <<'PY'
import re, json, urllib.request
text = open("bench/competitors/diy-faiss.hashes.txt").read()
pkgs = {n.lower(): v for n, v in re.findall(r"^([A-Za-z0-9_.-]+)==([^\s\\]+)", text, re.M)}
queries = [{"package": {"name": n, "ecosystem": "PyPI"}, "version": v} for n, v in sorted(pkgs.items())]
req = urllib.request.Request(
    "https://api.osv.dev/v1/querybatch",
    data=json.dumps({"queries": queries}).encode(),
    headers={"Content-Type": "application/json"},
)
with urllib.request.urlopen(req, timeout=60) as resp:
    data = json.load(resp)
for (name, ver), result in zip(sorted(pkgs.items()), data["results"]):
    if result.get("vulns"):
        print(name, ver, [v["id"] for v in result["vulns"]])
print(f"scanned {len(queries)}")
PY
```
