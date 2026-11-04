"""Pinned checkout of rekordcloud/openkeyscan-analyzer (models/manifest.json) in ml/third_party/.

Import this module, then `import model`, `import eval`, ... resolve to the upstream files.
Never copy upstream code into this repo; always go through the pinned checkout.
"""
import hashlib, json, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MANIFEST = json.loads((ROOT / "models" / "manifest.json").read_text())
SOURCE = MANIFEST["openkeyscan_source"]
CHECKPOINT = MANIFEST["openkeyscan3_pt"]
DIR = ROOT / "ml" / "third_party" / "openkeyscan-analyzer"


def _git(*args):
    return subprocess.check_output(["git", "-C", str(DIR), *args], text=True).strip()


def ensure_checkout():
    if not (DIR / ".git").exists():
        DIR.parent.mkdir(parents=True, exist_ok=True)
        subprocess.check_call(["git", "clone", "--quiet", SOURCE["repo"], str(DIR)])
    if _git("rev-parse", "HEAD") != SOURCE["commit"]:
        subprocess.call(["git", "-C", str(DIR), "fetch", "--quiet", "origin", SOURCE["commit"]])
        subprocess.check_call(["git", "-C", str(DIR), "checkout", "--quiet", SOURCE["commit"]])
    ckpt = DIR / CHECKPOINT["path"]
    digest = hashlib.sha256(ckpt.read_bytes()).hexdigest()
    if digest != CHECKPOINT["sha256"]:
        sys.exit(f"checkpoint sha256 mismatch: {digest} != {CHECKPOINT['sha256']}")
    if str(DIR) not in sys.path:
        sys.path.insert(0, str(DIR))
    return DIR, ckpt


ensure_checkout()
