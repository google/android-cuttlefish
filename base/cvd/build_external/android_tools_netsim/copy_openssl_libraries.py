import argparse
import pathlib
import shutil


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--output",
        type=pathlib.Path,
        help="Output directory"
    )
    parser.add_argument(
        "libraries",
        nargs="+",
        type=pathlib.Path,
        help="A list of library paths"
    )
    args = parser.parse_args()

    for library in args.libraries:
      library_name = library.name
      if library_name.endswith(".pic.a"):
        library_name = library_name.removesuffix(".pic.a") + ".a"
      output_path = args.output / library_name
      shutil.copy2(library, output_path)


if __name__ == "__main__":
    main()
