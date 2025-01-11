import argparse
import re
import subprocess
from typing import Optional, Tuple

VERSION_FILE = "VERSION"


def update_sdk_version(version_content: list[str]) -> Tuple[list[str], Optional[str]]:
    updated_content: list[str] = []
    current_version: Optional[str] = None
    new_version: Optional[str] = None

    for line in [line.strip() for line in version_content]:
        parts = line.split("=")
        if len(parts) == 2 and parts[0] == "ZAKURO_VERSION":
            version = parts[1]
            current_version = version
            version_match = re.match(r"(\d{4}\.\d+\.\d+)(-canary\.(\d+))?", version)
            if version_match:
                major_minor_patch = version_match.group(1)
                canary_part = version_match.groups()[1]
                canary_num = version_match.groups()[2]

                if canary_part is None:
                    new_version = f"{major_minor_patch}-canary.0"
                else:
                    new_version = f"{major_minor_patch}-canary.{int(canary_num) + 1}"
                updated_content.append(f"ZAKURO_VERSION={new_version}")
            else:
                updated_content.append(line)
        else:
            updated_content.append(line)

    if current_version is None:
        raise ValueError("ZAKURO_VERSION not found in VERSION file.")

    print(f"Current version: {current_version}")
    print(f"New version: {new_version}")

    confirmation = input("Do you want to update the version? (y/N): ").strip().lower()
    if confirmation != "y":
        print("Version update cancelled.")
        return updated_content, None

    return updated_content, new_version


def write_version_file(filename: str, updated_content: list[str], dry_run: bool) -> None:
    if dry_run:
        print(f"Dry run: The following content would be written to {filename}:")
        for line in updated_content:
            print(line)
    else:
        with open(filename, "w", encoding="utf-8") as file:
            file.write("\n".join(updated_content) + "\n")
        print(f"{filename} has been updated.")


def git_operations(new_version: str, dry_run: bool) -> None:
    if dry_run:
        print("Dry run: Would execute the following git commands:")
        print("- git commit -am '[canary] Update VERSION and examples/VERSION'")
        print(f"- git tag {new_version}")
        print("- git push")
        print(f"- git push origin {new_version}")
    else:
        print("Executing: git commit -am 'Update VERSION and examples/VERSION'")
        subprocess.run(
            ["git", "commit", "-am", "[canary] Update VERSION and examples/VERSION"], check=True
        )

        print(f"Executing: git tag {new_version}")
        subprocess.run(["git", "tag", new_version], check=True)

        print("Executing: git push")
        subprocess.run(["git", "push"], check=True)

        print(f"Executing: git push origin {new_version}")
        subprocess.run(["git", "push", "origin", new_version], check=True)


def main() -> None:
    parser = argparse.ArgumentParser(description="Update VERSION file and perform git operations.")
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Show what would be done without making any actual changes",
    )
    args = parser.parse_args()

    # Read and update the VERSION file
    with open(VERSION_FILE, "r") as file:
        version_content = file.readlines()
    updated_version_content, new_version = update_sdk_version(version_content)
    write_version_file(VERSION_FILE, updated_version_content, args.dry_run)

    # Perform git operations
    if new_version:
        git_operations(new_version, args.dry_run)


if __name__ == "__main__":
    main()
