# Kepler

A small TCP server written in C11 powered by libuv, an asynchronous networking library. It powers the Habbo Hotel version 18 client from 2007 era. The aim is to fully replicate this version by writing the back-end server in the C11 language.

This was the original Kepler emulator before [it was recoded](http://github.com/Quackster/Kepler) in Java. 

## Requirements

This server is only supported on Linux/POSIX systems. For Windows users, to use Kepler you must use the [Windows Subsystem for Linux](https://docs.microsoft.com/en-us/windows/wsl/install-win10) in order to run and compile the app in a Linux environment.

We will refer to Windows Subsystem for Linux as WSL from this point

There's an step-by-step guide on WSL [here](http://wsl-guide.org/en/latest/).

#### Linux distribution version requirements
One of these, the choice is yours:
- Arch Linux
- Ubuntu 18.04+
- Fedora 28+ Workstation

Other distributions have not been tested.

#### Note on using Ubuntu in WSL
This only applies if you're using [WSL](https://en.wikipedia.org/wiki/Windows_Subsystem_for_Linux)

If your current version of Ubuntu in [WSL](https://en.wikipedia.org/wiki/Windows_Subsystem_for_Linux) is 17.10 or lower, it needs to be upgraded to 18.04.
Run these commands to upgrade.

First check if an upgrade is needed:
```
$ lsb_release -a
```

Do not continue further if the version printed equals 18.04 or higher

```
$ sudo apt update
$ sudo apt upgrade
$ sudo -S apt-mark hold procps strace sudo
$ sudo -S env RELEASE_UPGRADER_NO_SCREEN=1 do-release-upgrade -d
```

## Downloading dependencies

First, install everything required to compile Kepler.

Ubuntu/Debian:

```
$ sudo apt install make cmake
$ sudo apt install gcc
$ sudo apt install git
$ sudo apt install libuv1-dev
$ sudo apt install sqlite3 libsqlite3-dev
$ sudo apt install libsodium-dev
```

Fedora:

```
$ sudo dnf install make cmake
$ sudo dnf install gcc
$ sudo dnf install git
$ sudo dnf install libuv-devel
$ sudo dnf install sqlite sqlite-devel
$ sudo dnf install libsodium-devel
```

Arch Linux:

```
$ sudo pacman -S make cmake
$ sudo pacman -S gcc
$ sudo pacman -S git
$ sudo pacman -S libuv
$ sudo pacman -S sqlite
$ sudo pacman -S libsodium
```

## Cloning this repository

```
$ git clone --recursive https://github.com/Quackster/KeplerC
```

**or**

```
$ git clone https://github.com/Quackster/KeplerC
$ git submodule update --init --recursive
```

## Compile Kepler

These commands will compile and run Kepler, first make sure you're in the directory of Kepler.

```
$ cmake -S . -B build
$ cmake --build build
$ ./build/Kepler
```

Run the CMake configure command again after changing `CMakeLists.txt` or installing new development packages.
