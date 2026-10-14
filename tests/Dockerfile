FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    clang libclang-rt-18-dev make libc6-dev curl libcurl4-openssl-dev pkg-config python3 openssl ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Match the library shipped by build_deps.sh rather than Ubuntu's older MHD.
RUN curl -fsSL https://ftp.gnu.org/gnu/libmicrohttpd/libmicrohttpd-1.0.1.tar.gz -o /tmp/mhd.tar.gz \
    && tar -xzf /tmp/mhd.tar.gz -C /tmp \
    && cd /tmp/libmicrohttpd-1.0.1 \
    && CC=clang ./configure --disable-https --disable-examples --disable-doc \
    && make -j2 && make install && ldconfig \
    && rm -rf /tmp/mhd.tar.gz /tmp/libmicrohttpd-1.0.1

WORKDIR /src
CMD ["python3", "tests/run_http_tests.py"]
