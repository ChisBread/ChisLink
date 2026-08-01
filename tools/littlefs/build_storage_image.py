#!/usr/bin/env python3
import argparse
import csv
import os
import shutil
import subprocess
import sys
from pathlib import Path


LITTLEFS_PYTHON_VERSION = "0.13.3"
DEFAULT_BLOCK_SIZE = 4096
DEFAULT_NAME_MAX = 64
COMMANDS = {"pack", "extract", "list"}


def repo_root_from_script():
    return Path(__file__).resolve().parents[2]


def parse_size(value):
    text = value.strip()
    if not text:
        raise ValueError("empty size")
    suffix = text[-1].upper()
    if suffix in ("K", "M"):
        scale = 1024 if suffix == "K" else 1024 * 1024
        return int(text[:-1], 0) * scale
    return int(text, 0)


def partition_size_from_csv(path, partition):
    with path.open("r", encoding="utf-8", newline="") as fp:
        for row in csv.reader(fp):
            if not row:
                continue
            name = row[0].strip()
            if not name or name.startswith("#") or name != partition:
                continue
            if len(row) < 5:
                raise ValueError(f"partition row is too short: {row}")
            return parse_size(row[4])
    raise ValueError(f"partition not found: {partition}")


def sdkconfig_value(path, key):
    if not path.exists():
        return None
    prefix = f"{key}="
    with path.open("r", encoding="utf-8") as fp:
        for line in fp:
            line = line.strip()
            if line.startswith(prefix):
                return line[len(prefix):].strip().strip('"')
    return None


def resolve_name_max(args, repo_root):
    if args.name_max is not None:
        return args.name_max
    value = sdkconfig_value(repo_root / "chislink-mcu" / "sdkconfig",
                            "CONFIG_LITTLEFS_OBJ_NAME_LEN")
    return int(value, 0) if value else DEFAULT_NAME_MAX


def executable_in_venv(venv):
    if os.name == "nt":
        return venv / "Scripts" / "littlefs-python.exe"
    return venv / "bin" / "littlefs-python"


def check_littlefs_python_version(tool, allow_mismatch):
    result = subprocess.run(
        [str(tool), "--version"],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    version = result.stdout.strip() or result.stderr.strip()
    if version != LITTLEFS_PYTHON_VERSION and not allow_mismatch:
        raise RuntimeError(
            f"{tool} is littlefs-python {version}, expected "
            f"{LITTLEFS_PYTHON_VERSION}; use --allow-version-mismatch to force"
        )


def ensure_littlefs_python(args, repo_root):
    if args.littlefs_python:
        tool = Path(args.littlefs_python)
        if not tool.exists():
            raise FileNotFoundError(tool)
        check_littlefs_python_version(tool, args.allow_version_mismatch)
        return tool

    env_tool = os.environ.get("LITTLEFS_PYTHON")
    if env_tool:
        tool = Path(env_tool)
        if not tool.exists():
            raise FileNotFoundError(tool)
        check_littlefs_python_version(tool, args.allow_version_mismatch)
        return tool

    path_tool = shutil.which("littlefs-python")
    if path_tool:
        tool = Path(path_tool)
        check_littlefs_python_version(tool, args.allow_version_mismatch)
        return tool

    if args.no_bootstrap:
        raise RuntimeError(
            "littlefs-python was not found; remove --no-bootstrap or pass "
            "--littlefs-python"
        )

    venv = Path(args.venv)
    if not venv.is_absolute():
        venv = repo_root / venv
    tool = executable_in_venv(venv)
    if tool.exists():
        check_littlefs_python_version(tool, args.allow_version_mismatch)
        return tool

    req = repo_root / "tools" / "littlefs" / "requirements.txt"
    venv.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([sys.executable, "-m", "venv", str(venv)], check=True)
    pip = venv / ("Scripts" if os.name == "nt" else "bin") / "pip"
    subprocess.run([str(pip), "install", "-r", str(req)], check=True)
    if not tool.exists():
        raise RuntimeError(f"littlefs-python was not installed at {tool}")
    check_littlefs_python_version(tool, args.allow_version_mismatch)
    return tool


def add_tool_args(parser):
    parser.add_argument(
        "--littlefs-python",
        help="path to a littlefs-python executable",
    )
    parser.add_argument(
        "--venv",
        default=str(Path.home() / ".cache" / "chislink" /
                    f"littlefs-python-{LITTLEFS_PYTHON_VERSION}"),
        help="private bootstrap venv path used when littlefs-python is missing",
    )
    parser.add_argument(
        "--no-bootstrap",
        action="store_true",
        help="fail instead of creating a private littlefs-python venv",
    )
    parser.add_argument(
        "--allow-version-mismatch",
        action="store_true",
        help="allow a littlefs-python version other than the pinned one",
    )


def add_littlefs_format_args(parser):
    parser.add_argument(
        "--block-size",
        type=parse_size,
        default=DEFAULT_BLOCK_SIZE,
        help="LittleFS block size",
    )
    parser.add_argument(
        "--name-max",
        type=int,
        help="LittleFS max object name length; defaults to sdkconfig or 64",
    )


def build_parser(repo_root):
    parser = argparse.ArgumentParser(
        description="Pack, extract, and inspect ChisLink LittleFS images"
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    pack = subparsers.add_parser(
        "pack",
        help="pack a directory into storage.bin",
        description="Pack a directory into a ChisLink LittleFS storage image",
    )
    pack.add_argument(
        "--source",
        default=str(repo_root / "chislink-mcu" / "flash_storage"),
        help="source directory to pack",
    )
    pack.add_argument(
        "--output",
        default=str(repo_root / "chislink-mcu" / "build" / "storage.bin"),
        help="output LittleFS image path",
    )
    pack.add_argument(
        "--partition-csv",
        default=str(repo_root / "chislink-mcu" / "partitions_littlefs.csv"),
        help="partition CSV used to read the storage partition size",
    )
    pack.add_argument(
        "--partition",
        default="storage",
        help="partition name in --partition-csv",
    )
    pack.add_argument(
        "--fs-size",
        type=parse_size,
        help="filesystem image size; overrides --partition-csv",
    )
    pack.add_argument(
        "--compact",
        action="store_true",
        help="pack data into the first blocks; off by default to match firmware",
    )
    pack.add_argument(
        "--no-pad",
        action="store_true",
        help="do not pad compact images to filesystem size",
    )
    add_littlefs_format_args(pack)
    add_tool_args(pack)

    extract = subparsers.add_parser(
        "extract",
        help="extract storage.bin to a directory",
        description="Extract a ChisLink LittleFS storage image",
    )
    extract.add_argument(
        "--image",
        default=str(repo_root / "chislink-mcu" / "build" / "storage.bin"),
        help="input LittleFS image path",
    )
    extract.add_argument(
        "--output",
        default=str(repo_root / "chislink-mcu" / "build" / "storage_extract"),
        help="destination directory",
    )
    add_littlefs_format_args(extract)
    add_tool_args(extract)

    list_cmd = subparsers.add_parser(
        "list",
        help="list files in storage.bin",
        description="List files in a ChisLink LittleFS storage image",
    )
    list_cmd.add_argument(
        "--image",
        default=str(repo_root / "chislink-mcu" / "build" / "storage.bin"),
        help="input LittleFS image path",
    )
    add_littlefs_format_args(list_cmd)
    add_tool_args(list_cmd)
    return parser


def normalize_argv(argv):
    if not argv:
        return ["pack"]
    if argv[0] in ("-h", "--help") or argv[0] in COMMANDS:
        return argv
    return ["pack"] + argv


def run_pack(args, repo_root):
    source = Path(args.source)
    output = Path(args.output)
    partition_csv = Path(args.partition_csv)
    if not source.exists():
        raise FileNotFoundError(source)
    if not source.is_dir():
        raise NotADirectoryError(source)
    if args.no_pad and not args.compact:
        raise ValueError("--no-pad requires --compact")

    fs_size = args.fs_size
    if fs_size is None:
        fs_size = partition_size_from_csv(partition_csv, args.partition)
    name_max = resolve_name_max(args, repo_root)

    tool = ensure_littlefs_python(args, repo_root)
    output.parent.mkdir(parents=True, exist_ok=True)

    cmd = [
        str(tool),
        "create",
        str(source),
        str(output),
        "-v",
        f"--fs-size={fs_size}",
        f"--name-max={name_max}",
        f"--block-size={args.block_size}",
    ]
    if args.compact:
        cmd.append("--compact")
    if args.no_pad:
        cmd.append("--no-pad")

    subprocess.run(cmd, check=True)
    print(
        f"storage image: {source} -> {output} "
        f"({fs_size} bytes, block={args.block_size}, name-max={name_max})"
    )


def run_extract(args, repo_root):
    image = Path(args.image)
    output = Path(args.output)
    if not image.exists():
        raise FileNotFoundError(image)

    name_max = resolve_name_max(args, repo_root)
    tool = ensure_littlefs_python(args, repo_root)
    output.mkdir(parents=True, exist_ok=True)
    cmd = [
        str(tool),
        "extract",
        "-v",
        f"--name-max={name_max}",
        f"--block-size={args.block_size}",
        str(image),
        str(output),
    ]
    subprocess.run(cmd, check=True)
    print(
        f"storage extract: {image} -> {output} "
        f"(block={args.block_size}, name-max={name_max})"
    )


def run_list(args, repo_root):
    image = Path(args.image)
    if not image.exists():
        raise FileNotFoundError(image)

    name_max = resolve_name_max(args, repo_root)
    tool = ensure_littlefs_python(args, repo_root)
    cmd = [
        str(tool),
        "list",
        "-v",
        f"--name-max={name_max}",
        f"--block-size={args.block_size}",
        str(image),
    ]
    subprocess.run(cmd, check=True)


def main():
    repo_root = repo_root_from_script()
    parser = build_parser(repo_root)
    args = parser.parse_args(normalize_argv(sys.argv[1:]))

    if args.command == "pack":
        run_pack(args, repo_root)
    elif args.command == "extract":
        run_extract(args, repo_root)
    elif args.command == "list":
        run_list(args, repo_root)
    else:
        parser.error(f"unknown command: {args.command}")


if __name__ == "__main__":
    main()
