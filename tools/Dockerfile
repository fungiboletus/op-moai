# The Blackfin cross compiler (op1r-src/toolchain/Dockerfile) and, on top, gdb's Blackfin instruction-set simulator
# (bfin-elf-run, for `make bench`): docker build --target toolchain | sim. JOBS: make -j (2 by default: -j32 runs a
# 4 GB colima VM out of memory; the CI passes 4).

FROM debian:12 AS toolchain
ARG JOBS=2
RUN apt-get update && apt-get install -y build-essential wget xz-utils libgmp-dev libmpfr-dev libmpc-dev texinfo flex bison && rm -rf /var/lib/apt/lists/*
WORKDIR /build
RUN wget -q https://ftp.gnu.org/gnu/binutils/binutils-2.40.tar.xz && tar xf binutils-2.40.tar.xz \
 && mkdir bb && cd bb && ../binutils-2.40/configure --target=bfin-elf --prefix=/opt/bfin --disable-nls --disable-werror --disable-gdb --disable-sim \
 && make -j$JOBS && make install && cd .. && rm -rf bb binutils-2.40*
ENV PATH=/opt/bfin/bin:$PATH
RUN wget -q https://ftp.gnu.org/gnu/gcc/gcc-12.3.0/gcc-12.3.0.tar.xz && tar xf gcc-12.3.0.tar.xz \
 && mkdir bg && cd bg && ../gcc-12.3.0/configure --target=bfin-elf --prefix=/opt/bfin --enable-languages=c --without-headers --with-newlib --disable-nls --disable-libssp --disable-libquadmath --disable-libgomp --disable-shared --disable-threads \
 && make -j$JOBS all-gcc all-target-libgcc && make install-gcc install-target-libgcc && cd .. && rm -rf bg gcc-12.3.0*
WORKDIR /work

FROM toolchain AS sim
ARG JOBS=2
RUN apt-get update && apt-get install -y zlib1g-dev && rm -rf /var/lib/apt/lists/*
WORKDIR /build
RUN wget -q https://ftp.gnu.org/gnu/gdb/gdb-13.2.tar.xz && tar xf gdb-13.2.tar.xz \
 && mkdir bs && cd bs && ../gdb-13.2/configure --target=bfin-elf --prefix=/opt/bfin --disable-nls --disable-werror \
      --disable-gdb --disable-gdbserver --disable-binutils --disable-ld --disable-gas --disable-gprof --enable-sim \
 && make -j$JOBS all-sim && make install-sim && cd .. && rm -rf bs gdb-13.2*
WORKDIR /work
