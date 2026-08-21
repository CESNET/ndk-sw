# SPDX-License-Identifier: BSD-3-Clause
# Copyright (C) 2026 CESNET z. s. p. o.

"""Contract for optional exception bridges used by the libnfb Python shim.

Shim callbacks return into C and must not leave a Python exception set. A
registered bridge may intercept selected BaseExceptions, clear them for the
C return path, and re-raise them later. Concrete policy is implemented
elsewhere.
"""

from __future__ import annotations

from abc import ABC, abstractmethod


class ExceptionBridgeBase(ABC):
    """Base for bridges registered via ``set_exception_bridge``."""

    @abstractmethod
    def pending(self) -> bool:
        """True if a prior intercept is still waiting to be re-raised."""

    @abstractmethod
    def intercept(self, exc: BaseException) -> bool:
        """Handle *exc* for a C callback.

        Return True if the shim should treat the call as a C error (exception
        cleared). Return False to leave handling to the shim (re-raise).
        """

    def raise_if_cancelled(self) -> None:
        """Re-raise a stashed cancel/interrupt if any (default: no-op)."""

    def clear(self) -> None:
        """Drop any stashed exception without raising (default: no-op)."""
