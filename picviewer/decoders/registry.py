"""Decoder dispatch: sniff the file, rank the candidates, try until one wins."""

from __future__ import annotations

import logging
from pathlib import Path

from .. import sniff
from .base import DecodedImage, Decoder
from .pillow_decoder import HeifDecoder, PillowDecoder, register_heif
from .raw_decoder import RawDecoder

log = logging.getLogger(__name__)


class DecoderRegistry:
    def __init__(self) -> None:
        self._decoders: list[Decoder] = []
        self.heif_available = register_heif()

        self.register(PillowDecoder())
        if self.heif_available:
            self.register(HeifDecoder())
        self.register(RawDecoder())

    def register(self, decoder: Decoder) -> None:
        self._decoders.append(decoder)

    def candidates(self, path: Path) -> list[Decoder]:
        """Decoders that might read *path*, most confident first."""
        container = sniff.sniff(path)
        ranked = [(d.score(path, container), d) for d in self._decoders]
        ranked = [(s, d) for s, d in ranked if s > 0]
        ranked.sort(key=lambda pair: pair[0], reverse=True)
        return [d for _, d in ranked]

    def load_preview(
        self, path: Path, max_size: tuple[int, int]
    ) -> DecodedImage | None:
        """Fast stand-in decode, or None when no decoder offers one."""
        container = sniff.sniff(path)
        for decoder in self.candidates(path):
            try:
                result = decoder.load_preview(path, container, max_size)
            except Exception:
                log.debug("%s preview failed for %s", decoder.name, path, exc_info=True)
                continue
            if result is not None:
                return result
        return None

    def load_full(
        self, path: Path, max_size: tuple[int, int] | None = None
    ) -> DecodedImage:
        """Accurate decode. Raises UnsupportedImage if every candidate fails."""
        # Establish that the bytes are reachable before blaming any decoder.
        # Otherwise an unreadable file surfaces as a decoder error such as
        # "heif: [Errno 13] Permission denied", which points at the format
        # when the real problem is the filesystem.
        _check_readable(path)

        container = sniff.sniff(path)
        candidates = self.candidates(path)
        if not candidates:
            raise UnsupportedImage(f"No decoder claims {path.name} ({container})")

        errors: list[str] = []
        for decoder in candidates:
            try:
                result = decoder.load_full(path, container, max_size)
            except Exception as exc:
                log.warning(
                    "%s failed on %s (%s): %s", decoder.name, path.name, container, exc
                )
                errors.append(f"{decoder.name}: {exc}")
                continue

            # Which decoder won matters when diagnosing a wrong-looking image:
            # a TIFF-based RAW handled by "pillow" means the small embedded
            # preview got through instead of the photo.
            result.metadata.setdefault("decoder", decoder.name)
            return result

        log.error("no decoder could read %s (%s)", path.name, container)
        raise UnsupportedImage(
            f"Could not decode {path.name} ({container})\n" + "\n".join(errors)
        )

    def decoder_names(self) -> list[str]:
        return [d.name for d in self._decoders]

    def supported_extensions(self) -> set[str]:
        exts: set[str] = set()
        for decoder in self._decoders:
            exts |= getattr(decoder, "extensions", set())
        from .raw_decoder import EXTENSIONS as RAW_EXTENSIONS

        return exts | RAW_EXTENSIONS


class UnsupportedImage(Exception):
    """No registered decoder could read the file."""


class UnreadableFile(UnsupportedImage):
    """The bytes could not be reached -- permissions, a lock, or a bad path."""


def _check_readable(path: Path) -> None:
    """Raise UnreadableFile with an explanation the user can act on."""
    try:
        with open(path, "rb") as fh:
            fh.read(1)
    except PermissionError:
        log.debug("permission denied reading %s", path)
        raise UnreadableFile(
            "Windows refused access to this file.\n\n"
            "It is usually one of:\n"
            "  - a OneDrive or other cloud placeholder the sync engine "
            "cannot fetch (try 'Always keep on this device')\n"
            "  - the file is open exclusively in another program\n"
            "  - the file permissions do not allow reading"
        ) from None
    except FileNotFoundError:
        raise UnreadableFile("The file no longer exists.") from None
    except OSError as exc:
        log.debug("cannot read %s: %s", path, exc)
        raise UnreadableFile("The file could not be read: " + str(exc)) from None


_registry: DecoderRegistry | None = None


def get_registry() -> DecoderRegistry:
    global _registry
    if _registry is None:
        _registry = DecoderRegistry()
    return _registry
