# Module 2: YOLO Analysis

This module implements the second-stage coarse classification and diff-analysis chain.

Current status:

- it still contains the existing heuristic `event_detector`
- it now contains `YoloDiffAnalyzer` for detection matching, event reasoning, and crop planning
- it now contains `YoloRuntime`, which reads the configured model asset and reports whether the current C++ runtime can execute it
- it now contains module-2 preprocessing and ONNX-output decoding, so the chain from `GrayFrame` to `YoloDetection` is defined inside C++
- it also contains debug artifact/report generation
- runtime config and diff-analysis config are now merged into one file: `cpp/configs/module_2_yolo.cfg`
- the repository currently contains both `models/best.pt` and `models/best.onnx`, and module-2 debug checks the configured ONNX asset directly
- real YOLO inference runtime is still pending; the ONNX asset still needs a dedicated C++ inference backend to execute the graph itself
