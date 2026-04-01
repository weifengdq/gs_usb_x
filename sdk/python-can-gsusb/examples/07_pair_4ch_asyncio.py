from pathlib import Path
import runpy


if __name__ == "__main__":
    runpy.run_path(str(Path(__file__).with_name("04_multi_std_asyncio.py")), run_name="__main__")