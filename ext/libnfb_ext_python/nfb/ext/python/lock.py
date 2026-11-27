"Component feature locks with the semantics of the NFB kernel driver (nfb_comp_lock)"

import threading


class LockTable:
    """Component feature locks of one device, shared by all its users.

    A lock is identified by the component path and a bitmask of features.
    The owner is one opened device (one ``nfb_open``, one connected client, ...).

    - ``lock`` fails when any owner (also the caller: not recursive)
      already holds one of the requested features of the component
    - ``unlock`` clears the requested features held by the caller only
    - ``release`` drops all features held by the owner (closed device,
      disconnected client), like the driver does when its file is released

    The table is thread safe.
    """

    def __init__(self):
        self._locks = {}  # (owner, path) -> features
        self._mutex = threading.Lock()

    def lock(self, owner, path: str, features: int) -> bool:
        """Lock the features of the component for the owner, return False when busy."""
        with self._mutex:
            for (_, p), f in self._locks.items():
                if p == path and f & features:
                    return False

            key = (owner, path)
            self._locks[key] = self._locks.get(key, 0) | features
            return True

    def unlock(self, owner, path: str, features: int) -> bool:
        """Unlock the features of the component held by the owner, return False when it holds none."""
        with self._mutex:
            key = (owner, path)
            if key not in self._locks:
                return False

            remaining = self._locks[key] & ~features
            if remaining:
                self._locks[key] = remaining
            else:
                del self._locks[key]
            return True

    def release(self, owner) -> None:
        """Drop all locks of the owner."""
        with self._mutex:
            for key in [k for k in self._locks if k[0] == owner]:
                del self._locks[key]
