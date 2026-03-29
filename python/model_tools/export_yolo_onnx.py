from __future__ import annotations

import argparse
from pathlib import Path
import sys


def parse_args() -> argparse.Namespace:
    repo_root = Path(__file__).resolve().parents[2]
    default_input = repo_root / "models" / "best.pt"
    default_output = repo_root / "models" / "best.onnx"

    parser = argparse.ArgumentParser(
        description="Export a YOLO .pt model to ONNX for the C++ pipeline."
    )
    parser.add_argument(
        "--input",
        type=Path,
        default=default_input,
        help="Path to the source .pt model.",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=default_output,
        help="Path to the target .onnx model.",
    )
    parser.add_argument(
        "--imgsz",
        type=int,
        default=640,
        help="Square inference size used during export.",
    )
    parser.add_argument(
        "--opset",
        type=int,
        default=12,
        help="ONNX opset version.",
    )
    parser.add_argument(
        "--simplify",
        action="store_true",
        help="Request graph simplification during export.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    input_path = args.input.resolve()
    output_path = args.output.resolve()

    if not input_path.exists():
        print(f"Input model does not exist: {input_path}", file=sys.stderr)
        return 1

    try:
        from ultralytics import YOLO
    except ImportError:
        print(
            "ultralytics is not installed. Run `py -m pip install -U ultralytics` first.",
            file=sys.stderr,
        )
        return 2

    output_path.parent.mkdir(parents=True, exist_ok=True)

    model = YOLO(str(input_path))
    exported_path = model.export(
        format="onnx",
        imgsz=args.imgsz,
        opset=args.opset,
        simplify=args.simplify,
    )

    exported_path = Path(exported_path).resolve()
    if exported_path != output_path:
        if output_path.exists():
            output_path.unlink()
        exported_path.replace(output_path)

    print(f"Exported ONNX model: {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
