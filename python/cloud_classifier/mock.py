from __future__ import annotations

from pathlib import Path
from typing import Any, Mapping, Sequence

from .config import RecognizerConfig
from .base import FineGrainedRecognizer, RecognitionResult, make_result


class MockRecognizer(FineGrainedRecognizer):
    provider = "mock"
    _unknown_tokens = frozenset({"unknown", "other", "others", "misc", "miscellaneous"})

    def __init__(self, config: RecognizerConfig | None = None) -> None:
        super().__init__(config=config or RecognizerConfig(provider=self.provider))

    def recognize_crop(
        self,
        image_path: str,
        coarse_class: str,
        candidate_labels: Sequence[str],
        inventory_context: Mapping[str, Any] | None = None,
    ) -> RecognitionResult:
        return self._run_with_call_log(
            coarse_class=coarse_class,
            candidate_labels=candidate_labels,
            impl=lambda: self._recognize_crop_impl(
                image_path=image_path,
                coarse_class=coarse_class,
                candidate_labels=candidate_labels,
                inventory_context=inventory_context,
            ),
        )

    def _recognize_crop_impl(
        self,
        image_path: str,
        coarse_class: str,
        candidate_labels: Sequence[str],
        inventory_context: Mapping[str, Any] | None = None,
    ) -> RecognitionResult:
        labels = self._normalize_labels(candidate_labels)
        category = self._normalize_text(coarse_class) or "unknown"
        context = inventory_context or {}

        if not labels:
            return make_result(
                name="unknown",
                category=category,
                confidence=0.0,
                reason="No candidate labels were provided to the mock recognizer.",
                is_unknown=True,
                provider=self.config.provider or self.provider,
            )

        preferred_label = self._find_preferred_label(labels, context)
        if preferred_label is not None:
            return self._known_result(
                name=preferred_label,
                category=category,
                confidence=0.96,
                reason="Matched inventory_context preferred label in candidate_labels.",
            )

        filename_hint = self._match_filename_hint(labels, image_path)
        if filename_hint is not None:
            return self._known_result(
                name=filename_hint,
                category=category,
                confidence=0.94,
                reason="Matched candidate label from the image_path filename hint.",
            )

        if len(labels) == 1:
            return self._known_result(
                name=labels[0],
                category=category,
                confidence=0.9,
                reason="Only one candidate label was provided.",
            )

        if category == "unknown":
            return make_result(
                name="unknown",
                category="unknown",
                confidence=0.0,
                reason="Coarse class is unknown, so the mock recognizer will not guess.",
                is_unknown=True,
                provider=self.config.provider or self.provider,
            )

        return self._known_result(
            name=labels[0],
            category=category,
            confidence=0.78,
            reason="Mock recognizer deterministically selected the first candidate label.",
        )

    def _known_result(self, name: str, category: str, confidence: float, reason: str) -> RecognitionResult:
        if self._canonical_text(name) in self._unknown_tokens:
            return make_result(
                name="unknown",
                category=category or "unknown",
                confidence=0.0,
                reason="Candidate label resolved to an unknown placeholder.",
                is_unknown=True,
                provider=self.config.provider or self.provider,
            )

        return make_result(
            name=name,
            category=category or "unknown",
            confidence=confidence,
            reason=reason,
            is_unknown=False,
            provider=self.config.provider or self.provider,
        )

    def _find_preferred_label(self, labels: Sequence[str], inventory_context: Mapping[str, Any]) -> str | None:
        if not isinstance(inventory_context, Mapping):
            return None

        candidates = {
            self._canonical_text(label): label
            for label in labels
        }
        preferred_keys = ("preferred_label", "expected_name", "last_seen_name")
        for key in preferred_keys:
            preferred = self._normalize_text(inventory_context.get(key))
            if not preferred:
                continue
            matched = candidates.get(self._canonical_text(preferred))
            if matched is not None:
                return matched
        return None

    def _match_filename_hint(self, labels: Sequence[str], image_path: str) -> str | None:
        filename_token = self._tokenize(Path(image_path).stem)
        if not filename_token:
            return None

        for label in labels:
            label_token = self._tokenize(label)
            if label_token and label_token in filename_token:
                return label
        return None

    def _normalize_labels(self, candidate_labels: Sequence[str]) -> list[str]:
        normalized = []
        seen = set()
        for raw_label in candidate_labels or []:
            label = self._normalize_text(raw_label)
            if not label:
                continue
            canonical = self._canonical_text(label)
            if canonical in seen:
                continue
            seen.add(canonical)
            normalized.append(label)
        return normalized

    def _normalize_text(self, value: Any) -> str:
        if value is None:
            return ""
        return str(value).strip()

    def _canonical_text(self, value: Any) -> str:
        return self._normalize_text(value).lower()

    def _tokenize(self, value: str) -> str:
        return "".join(ch.lower() for ch in value if ch.isalnum())
