import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def generate_documentation(sdk: Path, source_root: Path) -> None:
    suffix = ".exe" if os.name == "nt" else ""
    tools = {name: sdk / f"{name}{suffix}" for name in ("nomad", "nomadc")}
    for tool in tools.values():
        if not tool.is_file():
            raise FileNotFoundError(f"Missing SDK tool: {tool}")

    output = source_root / "out" / "documentation"
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="generation-", dir=output) as directory:
        staging = Path(directory)
        pages = staging / "source"
        pages.mkdir()
        for page in (source_root / "docs").glob("*.md"):
            if page.name != "releases.md":
                shutil.copy2(page, pages / page.name)

        project = staging / "project"
        subprocess.run(
            [str(tools["nomad"]), "init", str(project)], check=True, timeout=60
        )
        reference = pages / "engine-api.md"
        subprocess.run(
            [str(tools["nomadc"]), "docs", str(project), "--output", str(reference)],
            check=True,
            timeout=60,
        )
        if reference.stat().st_size == 0:
            raise ValueError("Generated engine API documentation is empty")

        destination = output / "source"
        if destination.exists():
            shutil.rmtree(destination)
        shutil.copytree(pages, destination)
    print(f"Documentation source prepared in {destination}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Prepare Markdown using a Nomad SDK")
    parser.add_argument("--sdk", required=True, type=Path, help="Extracted SDK directory")
    arguments = parser.parse_args()
    generate_documentation(arguments.sdk.resolve(), Path(__file__).resolve().parents[1])


if __name__ == "__main__":
    main()
