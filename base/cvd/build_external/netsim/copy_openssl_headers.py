import argparse
import pathlib
import shutil


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--out",
        type=pathlib.Path,
        help="Output directory"
    )
    parser.add_argument(
        "--includes",
        nargs="+",
        type=pathlib.Path,
        help="A list of source  paths for includes"
    )
    parser.add_argument(
        "--headers",
        nargs="+",
        type=pathlib.Path,
        help="A list of header paths."
    )
    args = parser.parse_args()

    for header_path in args.headers:
        found_match = False
        for src_include in args.includes:
            if not header_path.is_relative_to(src_include):
                continue
            new_dst_path = args.out / header_path.relative_to(src_include)
            new_dst_path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(header_path, new_dst_path)
            found_match = True
            break

        if not found_match:
            # ignore this file; it is not a public header
            pass


if __name__ == "__main__":
    main()
