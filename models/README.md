# models

This directory stores deployment-ready model assets used by the C++ pipeline.

## Current Contents

- `best.pt`: the source YOLO PyTorch weights
- `best.onnx`: the exported deployment asset for later C++ integration

## Current Limit

- the C++ repository can now detect and validate that the file exists
- the current runtime still does not execute end-to-end inference yet
- the next deployment step is to connect an ONNX inference backend in C++
