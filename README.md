# UamiOS

Made an operating system. I based if off of [NanoByte](https://youtube.com/playlist?list=PLFjM7v6KGMpiH2G-kT781ByCNC_0pKpPN&si=ywS0AYSzldNTsXI4)'s tutorial and from there I added a shell and stuff. I implemented memory management, CPU scheduling, interrupt handling, threading, and a FAT32 filesystem.

## Showcase
<img width="1280" height="720" alt="showcase" src="https://github.com/user-attachments/assets/3253b5b1-5eb7-42fb-9895-78099951f1c3" />

> I will make a better showcase in the future

## Building

Here are the arch specific things you need. If you are on anything else, find the equivalent or add it here yourself...

```
yay -S gcc make bison flex libgmp-static libmpc mpfr texinfo nasm mtools qemu-system-x86 python3 scons # or paru
```
Then you must run `python3 -m pip install -r requirements.txt`

Next, modify the configuration in `build_scripts/config.py`. The most important is the `toolchain='../.toolchains'` option which sets where the toolchain will be downloaded and built. The default option is in the directory above where the repo is cloned, in a .toolchains directory, but you will get an error if this directory doesn't exist.

After that, run `scons toolchain`, this should download and build the required tools (binutils and GCC). If you encounter errors during this step, you might have to modify `scripts/setup_toolchain.sh` and try a different version of **binutils** and **gcc**. Using the same version as the one bundled with your distribution is your best bet.

Finally, you should be able to run `scons`. Use `scons run` to test your OS using qemu.
