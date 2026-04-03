# models

This directory stores deployment-ready model assets used by the C++ pipeline.

## Current Contents

- `best.pt`: the source YOLO PyTorch weights
- `best.onnx`: the exported deployment asset for later C++ integration

## Current Limit

- the C++ repository can execute `best.onnx` through OpenCV DNN when built with OpenCV support
- builds without OpenCV DNN still fall back to mock/debug paths and do not execute end-to-end inference
- `best.pt` is still kept as the original training/export source asset
