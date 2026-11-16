FROM debian:bookworm AS build

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential cmake pkg-config git ca-certificates \
      libglib2.0-dev libusb-1.0-0-dev libusb-dev \
 && rm -rf /var/lib/apt/lists/*

ARG CMAKE_MODULES_TAG=versions/1.0.0--rc6
ARG NOVACOMD_TAG=versions/2.0.0
ARG NOVACOM_TAG=versions/1.1.0-rc1

WORKDIR /src
RUN git clone https://github.com/openwebos/cmake-modules-webos \
 && git -C cmake-modules-webos checkout ${CMAKE_MODULES_TAG} \
 && cmake -S cmake-modules-webos -B cmake-modules-webos/build \
 && cmake --install cmake-modules-webos/build

# -fcommon: old C code defining the same globals in several files (fails with GCC >= 10).
# STACK_SIZE: the original 64 KiB thread stack is too small for a modern glibc (pthread_create fails).
RUN git clone https://github.com/openwebos/novacomd \
 && git -C novacomd checkout ${NOVACOMD_TAG} \
 && sed -i 's/#define STACK_SIZE (64\*1024)/#define STACK_SIZE (1024*1024)/' novacomd/src/platform_pthreads.c \
 && cmake -S novacomd -B novacomd/build -DWEBOS_TARGET_MACHINE_IMPL=host -DCMAKE_C_FLAGS=-fcommon \
 && cmake --build novacomd/build -j"$(nproc)" \
 && cmake --install novacomd/build

RUN git clone https://github.com/openwebos/novacom \
 && git -C novacom checkout ${NOVACOM_TAG} \
 && make -C novacom CFLAGS="-w -fcommon -DBUILDVERSION=\\\"novacom-${NOVACOM_TAG}\\\"" \
 && install -D novacom/build-novacom/novacom /usr/local/webos/bin/novacom

FROM debian:bookworm
RUN apt-get update && apt-get install -y --no-install-recommends \
      libglib2.0-0 libusb-1.0-0 libusb-0.1-4 usbutils dfu-util \
 && rm -rf /var/lib/apt/lists/*
COPY --from=build /usr/local/webos /usr/local/webos
ENV PATH=/usr/local/webos/sbin:/usr/local/webos/bin:$PATH
WORKDIR /work
