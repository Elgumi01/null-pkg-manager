# npkg

A minimalist and lightweight package manager written in C for NullOS.

## About

`npkg` is the package manager developed for NullOS.

Its goal is to provide a simple and predictable way to manage packages
that are built locally from package recipes.

Rather than providing pre-built binaries, `npkg` manages the process of
downloading, building, installing, tracking and removing packages and
their dependencies.

## Features

- Package installation and removal
- Dependency resolution
- Package searching
- Installed package tracking
- Installation manifests
- `DESTDIR` support
- Build cache management
- Package recipe system based on JSON
- Shell-free process execution
- Basic environment sanitization
- Atomic manifest write
- Written entirely in C

## Dependencies

The only dependency required to build `npkg` itself is:

- [cJSON](https://github.com/DaveGamble/cJSON)

## Recommended Dependencies

Some package recipes require additional tools to build successfully.

### Build tools

```text
meson
xz
python3
```

### Libraries and utilities

```text
git
zlib
bzip2
flex
```

### Python packages

```text
setuptools
pip
wheel
```

These dependencies are not required by `npkg` itself. They are required
by some of the packages available in the `packages/` directory.

## Installation

Clone the `null-package-manager` repository:

```bash
git clone https://github.com/Elgumi01/null-package-manager.git
```

Clone the official `null-packages` repository:

```bash
git clone https://github.com/Elgumi01/null-packages.git
```

Copy the package recipes into the `npkg` package directory:

```bash
sudo cp -r null-packages/packages/* /etc/npkg/packages/
```

This will make all packages from `null-packages` available to `npkg`.

Build:

```bash
cd null-package-manager/
make
```

Install:

```bash
sudo make install
```
You can then check that `npkg` is working:

```bash
npkg --help
```

## Usage

### Search for a package

```bash
npkg search vim
```

Example:

```text
[ ] vim 9.1
    Desc : Highly configurable text editor built to enable efficient text editing, with support for syntax highlighting, plugins, and terminal-based UI via ncurses.
```

### Install a package

```bash
sudo npkg install vim
```

Dependencies are automatically resolved and installed when required.

### Remove a package

```bash
sudo npkg remove vim
```

### List installed packages

```bash
npkg list
```

### Clear the build cache

```bash
sudo npkg --clear-cache
```

## DESTDIR

`npkg` supports installing packages into an alternative root using
`DESTDIR`.

For example:

```bash
sudo DESTDIR=/mnt/lfs npkg install vim
```

This allows packages to be installed into a staging or alternate
filesystem without modifying the host root filesystem.

## Package Recipes

Packages are defined using JSON recipes stored in the `packages/`
directory.

You are encouraged to create and maintain your own package recipes.

The purpose of `npkg` is not to provide every package configuration
ready-made, but to provide the tools necessary to manage manually
defined and locally compiled packages.

A package recipe describes information such as:

- Package name
- Version
- Description
- Source
- Dependencies
- Build instructions
- Installation instructions

## Package Dependencies

### Bootstrap / implicit toolchain

The following packages form the minimum system toolchain and should **not**
be listed in the `dependencies` field of package recipes:

```text
gcc
binutils
glibc
make
bash
coreutils
tar
gzip
xz
sed
grep
```

These packages are assumed to already exist before `npkg` is used.

Listing them as dependencies would only add unnecessary entries to the
dependency graph.

### Everything else is explicit

Libraries and build tools such as:

```text
zlib
ncurses
expat
gperf
meson
ninja
lzip
pkgconf
cmake
```

should remain explicitly listed in `dependencies`.

Even if a package is normally included in a standard LFS installation,
it should be declared when another package actually requires it.

This keeps dependency resolution predictable and avoids silently relying
on packages that may not exist in a different system configuration.

### Dependency rule

Before adding a dependency, ask:

> Is this part of the guaranteed bootstrap toolchain, or is it a library
> or tool that the package actually requires?

- Bootstrap toolchain → **omit**
- Everything else → **list explicitly**

## Project Structure

```text
null-package-manager/
├── LICENSE
├── Makefile
├── README.md
├── config/
│   ├── config.h
│   └── make.conf
├── include/
│   ├── clear_cache.h
│   ├── common/
│   │   ├── env.h
│   │   ├── lock.h
│   │   ├── log.h
│   │   ├── manifest.h
│   │   ├── paths.h
│   │   ├── proc.h
│   │   └── validate.h
│   ├── install.h
│   ├── list.h
│   ├── remove.h
│   ├── reset_db.h
│   └── search.h
├── packages/
└── src/
    ├── clear_cache.c
    ├── common/
    │   ├── env.c
    │   ├── lock.c
    │   ├── manifest.c
    │   ├── proc.c
    │   └── validate.c
    ├── install.c
    ├── list.c
    ├── main.c
    ├── remove.c
    ├── reset_db.c
    └── search.c
```

## Status

`npkg` is currently in active development.

The core package management workflow is functional, including package
installation, removal, searching, dependency resolution, manifests and
build-cache management.

## Contributing

Contributions, bug reports and package recipes are welcome.

If you add a new package, please follow the existing package recipe
structure and explicitly declare all non-bootstrap dependencies.

## License

This project is licensed under the MIT License.

See [LICENSE](LICENSE) for the full license text.
