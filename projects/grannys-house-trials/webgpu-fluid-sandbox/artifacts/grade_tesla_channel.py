"""Compatibility entry point for rebuilding the current stamped Tesla terrain."""
from pathlib import Path
from runpy import run_path

run_path(str(Path(__file__).with_name("stamp_tesla_blue_box.py")), run_name="__main__")
