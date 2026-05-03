# models

This directory stores deployment-ready model assets used by the C++ pipeline.

## Current Contents

- `best.pt`: the source YOLO PyTorch weights used for training/export
- `best.onnx`: the exported deployment asset used by the C++ module-2 runtime

## Current Limit

- module 2 prefers ONNX Runtime for executing `best.onnx` when CMake finds the ONNX Runtime SDK
- OpenCV DNN is the secondary fallback backend for `best.onnx`
- builds without ONNX Runtime and without OpenCV DNN still fall back to mock/debug/`.pgm` paths, but those paths do not execute the real ONNX graph
- `best.pt` is still kept as the original training/export source asset, not as a runtime asset for the C++ pipeline
