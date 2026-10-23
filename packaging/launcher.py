"""Entry point for the frozen build.

PyInstaller runs its target as a top-level script with no parent package, so
aiming it at picviewer/__main__.py makes every relative import in that file
fail with "attempted relative import with no known parent package". Importing
the package properly from a separate launcher is the fix.
"""

import sys

from picviewer.__main__ import main

if __name__ == "__main__":
    sys.exit(main())
