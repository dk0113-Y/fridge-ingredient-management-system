from __future__ import annotations

from dataclasses import replace
from typing import Any, Mapping, Sequence

from .config import RecognizerConfig
from .base import FineGrainedRecognizer, RecognitionResult


class XiaomiMimoRecognizer(FineGrainedRecognizer):
    provider = "xiaomi_mimo"

    def __init__(
        self,
        config: RecognizerConfig | None = None,
        api_key: str | None = None,
        endpoint: str | None = None,
        timeout_s: float = 15.0,
        model_name: str | None = None,
        max_retries: int | None = None,
        enable_base64_image: bool | None = None,
    ) -> None:
        effective_config = config or RecognizerConfig(provider=self.provider)
        if api_key is not None:
            effective_config = replace(effective_config, api_key=api_key)
        if endpoint is not None:
            effective_config = replace(effective_config, endpoint=endpoint)
        if model_name is not None:
            effective_config = replace(effective_config, model_name=model_name)
        if max_retries is not None:
            effective_config = replace(effective_config, max_retries=max_retries)
        if enable_base64_image is not None:
            effective_config = replace(effective_config, enable_base64_image=enable_base64_image)
        if timeout_s is not None:
            effective_config = replace(effective_config, timeout_ms=max(1, int(timeout_s * 1000)))
        super().__init__(config=effective_config)

    def recognize_crop(
        self,
        image_path: str,
        coarse_class: str,
        candidate_labels: Sequence[str],
        inventory_context: Mapping[str, Any] | None = None,
    ) -> RecognitionResult:
        del image_path, inventory_context
        return self._run_with_call_log(
            coarse_class=coarse_class,
            candidate_labels=candidate_labels,
            impl=self._recognize_crop_impl,
        )

    def _recognize_crop_impl(self) -> RecognitionResult:
        raise NotImplementedError(
            "XiaomiMimoRecognizer is a placeholder. Fill in the real endpoint and request logic before use."
        )
