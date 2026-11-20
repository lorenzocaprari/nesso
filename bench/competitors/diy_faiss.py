"""DIY sentence-transformers + FAISS baseline for scripts/eval-retrieval.

Heavy imports (torch, sentence-transformers, faiss) happen only when the engine
is constructed or prepared. Unit tests inject encode/index callables and never
need those packages.
"""

from __future__ import annotations

import os
import random
from collections.abc import Callable, Sequence
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

MODEL_ID = "sentence-transformers/all-MiniLM-L6-v2"
MODEL_REVISION = "1110a243fdf4706b3f48f1d95db1a4f5529b4d41"
SEED = 0
THREADS = 1
DEFAULT_K = 10

REQUIREMENTS_PATH = "bench/competitors/diy-faiss.hashes.txt"
INSTALL_HINT = (
    f"diy-faiss requires packages from {REQUIREMENTS_PATH}\n"
    "  python3 -m venv .venv-diy && .venv-diy/bin/pip install --require-hashes \\\n"
    f"    -r {REQUIREMENTS_PATH} \\\n"
    "    --extra-index-url https://download.pytorch.org/whl/cpu"
)


def top_k_line_ids(scores: Sequence[float], k: int = DEFAULT_K) -> list[int]:
    """Return up to k 1-based line ids ordered by (-score, line_id)."""
    if k <= 0 or not scores:
        return []
    ranked = sorted(
        ((float(score), line_id) for line_id, score in enumerate(scores, start=1)),
        key=lambda item: (-item[0], item[1]),
    )
    return [line_id for _, line_id in ranked[:k]]


def configure_thread_env() -> None:
    """Pin BLAS/OpenMP thread counts before importing numpy/faiss/torch."""
    value = str(THREADS)
    for key in ("OMP_NUM_THREADS", "MKL_NUM_THREADS", "OPENBLAS_NUM_THREADS"):
        os.environ[key] = value


def import_diy_stack() -> tuple[Any, Any, Any, Any]:
    """Import faiss, numpy, torch, SentenceTransformer or raise SystemExit."""
    configure_thread_env()
    try:
        import faiss  # type: ignore[import-untyped]
        import numpy as np
        import torch
        from sentence_transformers import SentenceTransformer
    except ImportError as exc:
        raise SystemExit(f"error: {INSTALL_HINT}") from exc
    return faiss, np, torch, SentenceTransformer


def apply_determinism(torch: Any, faiss: Any) -> None:
    random.seed(SEED)
    try:
        import numpy as np

        np.random.seed(SEED)
    except ImportError:
        pass
    torch.manual_seed(SEED)
    torch.set_num_threads(THREADS)
    faiss.omp_set_num_threads(THREADS)


EncodeFn = Callable[[Sequence[str]], Any]
MakeIndexFn = Callable[[Any], Any]


def load_model(sentence_transformers: Any) -> Any:
    return sentence_transformers(MODEL_ID, revision=MODEL_REVISION, device="cpu")


def encode_normalized(model: Any, np: Any, texts: Sequence[str]) -> Any:
    """Encode texts to an L2-normalized float32 (n, dim) matrix."""
    embeddings = model.encode(
        list(texts),
        normalize_embeddings=True,
        convert_to_numpy=True,
        show_progress_bar=False,
    )
    return np.asarray(embeddings, dtype=np.float32)


def make_encoder() -> Callable[[Sequence[str]], list[list[float]]]:
    """Return the diy-faiss MiniLM encoder as plain float lists (for other engines).

    Raises SystemExit with the install hint when the optional stack is missing.
    """
    faiss, np, torch, SentenceTransformer = import_diy_stack()
    apply_determinism(torch, faiss)
    model = load_model(SentenceTransformer)

    def encode(texts: Sequence[str]) -> list[list[float]]:
        if not texts:
            return []
        return encode_normalized(model, np, texts).tolist()

    return encode


@dataclass
class DiyFaissEngine:
    """Brute-force FAISS IP over sentence-transformers MiniLM embeddings."""

    name: str = "diy-faiss"
    encode_fn: EncodeFn | None = None
    make_index_fn: MakeIndexFn | None = None
    _model: Any = field(default=None, init=False, repr=False)
    _index: Any = field(default=None, init=False, repr=False)
    _np: Any = field(default=None, init=False, repr=False)
    _n: int = field(default=0, init=False)
    _faiss: Any = field(default=None, init=False, repr=False)
    _torch: Any = field(default=None, init=False, repr=False)
    _sentence_transformers: Any = field(default=None, init=False, repr=False)

    def availability(self) -> tuple[bool, str]:
        if self.encode_fn is not None and self.make_index_fn is not None:
            return True, ""
        try:
            self._ensure_stack()
        except SystemExit as exc:
            reason = exc.code if isinstance(exc.code, str) else INSTALL_HINT
            if reason.startswith("error: "):
                reason = reason[len("error: ") :]
            return False, reason
        return True, ""

    def _ensure_stack(self) -> None:
        if self._faiss is not None:
            return
        faiss, np, torch, SentenceTransformer = import_diy_stack()
        apply_determinism(torch, faiss)
        self._faiss = faiss
        self._np = np
        self._torch = torch
        self._sentence_transformers = SentenceTransformer

    def prepare(self, log_path: Path, lines: list[str], workdir: Path) -> None:
        del log_path, workdir
        if self.encode_fn is not None and self.make_index_fn is not None:
            embeddings = self.encode_fn(lines)
            self._index = self.make_index_fn(embeddings)
            self._n = len(lines)
            return
        self._ensure_stack()
        assert self._sentence_transformers is not None
        assert self._faiss is not None
        assert self._np is not None
        model = load_model(self._sentence_transformers)
        matrix = encode_normalized(model, self._np, lines)
        if matrix.ndim != 2 or matrix.shape[0] != len(lines):
            raise SystemExit(
                f"error: diy-faiss encode returned shape {getattr(matrix, 'shape', None)}; "
                f"expected ({len(lines)}, dim)"
            )
        index = self._faiss.IndexFlatIP(matrix.shape[1])
        index.add(matrix)
        self._model = model
        self._index = index
        self._n = len(lines)

    def rank(self, query: str, *, k: int = DEFAULT_K) -> list[int]:
        if self._index is None:
            raise SystemExit("error: diy-faiss engine not prepared")
        if k <= 0 or self._n == 0:
            return []
        if self.encode_fn is not None:
            query_vec = self.encode_fn([query])
            scores, indices = self._index.search(query_vec, self._n)
        else:
            assert self._model is not None
            assert self._np is not None
            query_vec = encode_normalized(self._model, self._np, [query])
            scores, indices = self._index.search(query_vec, self._n)
        row_scores = scores[0]
        row_indices = indices[0]
        scored_lines: list[tuple[float, int]] = []
        for score, index in zip(row_scores, row_indices, strict=True):
            if int(index) < 0:
                continue
            scored_lines.append((float(score), int(index) + 1))
        scored_lines.sort(key=lambda item: (-item[0], item[1]))
        return [line_id for _, line_id in scored_lines[:k]]

    def write_index(self, path: Path) -> None:
        if self._index is None:
            raise SystemExit("error: diy-faiss engine not prepared")
        if self.encode_fn is not None and self.make_index_fn is not None:
            path.write_bytes(b"fake-diy-faiss-index")
            return
        assert self._faiss is not None
        self._faiss.write_index(self._index, str(path))

    def read_index(self, path: Path, n: int) -> None:
        if self.encode_fn is not None and self.make_index_fn is not None:
            self._n = n
            return
        self._ensure_stack()
        assert self._faiss is not None
        self._index = self._faiss.read_index(str(path))
        self._n = n
        assert self._sentence_transformers is not None
        self._model = load_model(self._sentence_transformers)

    def versions(self) -> dict[str, str]:
        if self.encode_fn is not None and self.make_index_fn is not None:
            return {
                "model": MODEL_ID,
                "model_revision": MODEL_REVISION,
                "sentence_transformers": "fake",
                "torch": "fake",
                "faiss": "fake",
            }
        self._ensure_stack()
        import faiss  # type: ignore[import-untyped]
        import sentence_transformers
        import torch

        return {
            "model": MODEL_ID,
            "model_revision": MODEL_REVISION,
            "sentence_transformers": sentence_transformers.__version__,
            "torch": torch.__version__,
            "faiss": getattr(faiss, "__version__", "unknown"),
        }
