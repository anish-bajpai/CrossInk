# PlatformIO extra script (simulator): mirror SD layout under ./fs_/books/
# so /books/sentence_demo.txt resolves like on-device paths.
Import("env")  # noqa: F821

import shutil
from pathlib import Path

project_dir = Path(env["PROJECT_DIR"])
src = project_dir / "data" / "sentence_demo.txt"
dst = project_dir / "fs_" / "books" / "sentence_demo.txt"

if not src.is_file():
    print(f"ensure_sentence_demo_book: missing {src}")
else:
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)
    print(f"ensure_sentence_demo_book: {src} -> {dst}")
