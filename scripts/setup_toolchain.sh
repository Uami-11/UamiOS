#!/bin/bash
BINUTILS_VERSION="${BINUTILS_VERSION:-2.46.0}"
GCC_VERSION="${GCC_VERSION:-15.2.0}"
BINUTILS_URL="https://ftp.gnu.org/gnu/binutils/binutils-${BINUTILS_VERSION}.tar.xz"
GCC_URL="https://ftp.gnu.org/gnu/gcc/gcc-${GCC_VERSION}/gcc-${GCC_VERSION}.tar.xz"
TARGET=i686-elf
# ---------------------------
set -e
TOOLCHAINS_DIR=
OPERATION='build'
while test $# -gt 0; do
    case "$1" in
    -c)
        OPERATION='clean'
        ;;
    *)
        TOOLCHAINS_DIR=$(realpath "$1")
        ;;
    esac
    shift
done
if [ -z "$TOOLCHAINS_DIR" ]; then
    echo "Missing arg: toolchains directory"
    exit 1
fi

PROJECT_DIR=$(pwd)
AR_WRAPPER="$PROJECT_DIR/scripts/toolchain/ar-wrapper"

mkdir -p "$TOOLCHAINS_DIR"

# Set up wrappers directory on PATH so configure finds it by name
WRAPPERS_DIR="$TOOLCHAINS_DIR/wrappers"
mkdir -p "$WRAPPERS_DIR"
cp "$AR_WRAPPER" "$WRAPPERS_DIR/x86_64-pc-linux-gnu-ar"
cp "$AR_WRAPPER" "$WRAPPERS_DIR/ar" # <-- add this
chmod +x "$WRAPPERS_DIR/x86_64-pc-linux-gnu-ar"
chmod +x "$WRAPPERS_DIR/ar" # <-- add this
export PATH="$WRAPPERS_DIR:$PATH"

pushd "$TOOLCHAINS_DIR"
TOOLCHAIN_PREFIX="$TOOLCHAINS_DIR/$TARGET"

if [ "$OPERATION" = "build" ]; then
    # Download and build binutils
    BINUTILS_SRC="binutils-${BINUTILS_VERSION}"
    BINUTILS_BUILD="binutils-build-${BINUTILS_VERSION}"
    wget -N ${BINUTILS_URL}
    tar -xf binutils-${BINUTILS_VERSION}.tar.xz
    mkdir -p ${BINUTILS_BUILD}
    pushd ${BINUTILS_BUILD}
    ../binutils-${BINUTILS_VERSION}/configure \
        --prefix="${TOOLCHAIN_PREFIX}" \
        --target=${TARGET} \
        --with-sysroot \
        --disable-nls \
        --disable-werror \
        --disable-gold \
        --disable-plugins
    find . -name config.cache -exec sed -i "s|./toolchain/ar-wrapper|$AR_WRAPPER|g" {} \;
    find . -name libtool -exec sed -i "s|./toolchain/ar-wrapper|$AR_WRAPPER|g" {} \;
    make -j8
    make install
    popd
    # Download and build GCC
    GCC_SRC="gcc-${GCC_VERSION}"
    GCC_BUILD="gcc-build-${GCC_VERSION}"
    wget -N ${GCC_URL}
    tar -xf gcc-${GCC_VERSION}.tar.xz
    mkdir -p ${GCC_BUILD}
    pushd ${GCC_BUILD}
    ../gcc-${GCC_VERSION}/configure \
        --prefix="${TOOLCHAIN_PREFIX}" \
        --target=${TARGET} \
        --disable-nls \
        --enable-languages=c,c++ \
        --without-headers
    make -j8 all-gcc all-target-libgcc
    make install-gcc install-target-libgcc
    popd
elif [ "$OPERATION" = "clean" ]; then
    rm -rf *
fi
