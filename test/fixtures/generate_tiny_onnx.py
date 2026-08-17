#!/usr/bin/env python3
"""Emit a tiny MiniLM-shaped ONNX fixture: int64 inputs -> float32 [1, 384] output."""

from __future__ import annotations

import sys
from pathlib import Path

try:
    import numpy as np
    from onnx import TensorProto, helper, numpy_helper, save
except ImportError:
    sys.stderr.write("onnx and numpy are required to generate the fixture\n")
    raise SystemExit(1)


def main() -> None:
    out = Path(__file__).with_name("tiny_minilm.onnx")
    values = np.full((1, 384), 0.05, dtype=np.float32)
    values /= np.linalg.norm(values)

    node = helper.make_node(
        "Constant",
        inputs=[],
        outputs=["sentence_embedding"],
        value=numpy_helper.from_array(values, name="embedding"),
    )
    graph = helper.make_graph(
        nodes=[node],
        name="tiny_minilm",
        inputs=[
            helper.make_tensor_value_info("input_ids", TensorProto.INT64, [1, 128]),
            helper.make_tensor_value_info("attention_mask", TensorProto.INT64, [1, 128]),
        ],
        outputs=[
            helper.make_tensor_value_info("sentence_embedding", TensorProto.FLOAT, [1, 384]),
        ],
    )
    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 17)])
    model.ir_version = 8
    save(model, out)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
