CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -D_GNU_SOURCE
# the version printed by xisto --version: the release tag (make VERSION=0.1.0), else git describe, else "dev"
VERSION ?= $(shell git describe --tags --always --dirty 2>/dev/null | sed 's/^v//' || echo dev)
SRC     := $(wildcard src/*.c)
OBJ     := $(patsubst src/%.c,build/%.o,$(SRC)) build/rt_embed.o

# TLS backend: AWS-LC by default (TLS 1.3, assembly crypto; needs cmake, downloads a pinned release once).
# make TLS=bearssl skips it: xisto then links BearSSL (pure C, in the tree). Either is linked into a program
# only when it can use https; plain-http programs (TLS at the load balancer) carry no TLS code.
TLS ?= awslc

all: build/xisto build/qbe build/xisto_rt.o build/xisto_rt_dbg.o build/sqlite3.o build/sqlite3.h build/libxszlib.a build/libxsbrotli.a build/libbearssl.a $(if $(filter awslc,$(TLS)),build/awslc/lib/libssl.a)

build/awslc/lib/libssl.a: tools/build-awslc.sh
	sh tools/build-awslc.sh

awslc: build/awslc/lib/libssl.a

# BearSSL from source (third_party/bearssl, MIT), a static library: the linker takes only what is used
# getentropy(): one syscall to seed each connection's DRBG, instead of open/read/close of /dev/urandom
BEARSSL_FLAGS := -DBR_USE_GETENTROPY=1 $(if $(filter Darwin,$(shell uname)),-include sys/types.h -include sys/random.h)
# Xisto patches (third_party/bearssl/XISTO_PATCHES.md): native cipher-suite lookup in the handshake code,
# fixed-base comb for P-256 (ec_p256_m64_comb.h), 64-bit ECDSA signing tail, AArch64 field multiplication
# (the .S assembles to nothing on other architectures).
BEARSSL_SRC := $(shell find third_party/bearssl/src -name '*.c')
BEARSSL_ASM := $(shell find third_party/bearssl/src -name '*.S')
BEARSSL_OBJ := $(patsubst third_party/bearssl/src/%.c,build/bearssl/%.o,$(BEARSSL_SRC)) $(patsubst third_party/bearssl/src/%.S,build/bearssl/%.S.o,$(BEARSSL_ASM))
build/bearssl/%.o: third_party/bearssl/src/%.c
	@mkdir -p $(dir $@)
	$(CC) -O2 $(BEARSSL_FLAGS) -Ithird_party/bearssl/inc -Ithird_party/bearssl/src -c -o $@ $<
build/bearssl/%.S.o: third_party/bearssl/src/%.S
	@mkdir -p $(dir $@)
	$(CC) -c -o $@ $<
build/bearssl/ec/ec_p256_m64.o: third_party/bearssl/src/ec/ec_p256_m64_comb.h

build/libbearssl.a: $(BEARSSL_OBJ)
	rm -f $@ && ar rcs $@ $(BEARSSL_OBJ)
	rm -rf build/bearssl-inc && cp -r third_party/bearssl/inc build/bearssl-inc

# SQLite from source (third_party/sqlite, the amalgamation), compiled once; linked only when a program imports node:sqlite
build/sqlite3.o: third_party/sqlite/sqlite3.c | build
	$(CC) -O2 -DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_DEFAULT_MEMSTATUS=0 -DSQLITE_DQS=0 -c -o $@ $<

build/sqlite3.h: third_party/sqlite/sqlite3.h | build
	cp $< $@

# zlib 1.3.1 from source (third_party/zlib, zlib license), compiled once into a static library; linked only when a
# program imports node:zlib. Vendored rather than the system's -lz: the binaries then need no libz of a given
# version on the machine that runs them, and --target builds (musl, Windows) get the same code. Z_PREFIX renames
# its symbols (z_deflate...), so a system libz pulled in by something else cannot clash with it. ~50 KB of
# object code, of which a program takes only what it calls (an inflate-only program carries no deflate).
ZLIB_SRC := adler32 crc32 deflate inflate inffast inftrees trees zutil
ZLIB_OBJ := $(ZLIB_SRC:%=build/zlib/%.o)
build/zlib/%.o: third_party/zlib/%.c $(wildcard third_party/zlib/*.h)
	@mkdir -p $(dir $@)
	$(CC) -O2 -DZ_PREFIX -w -c -o $@ $<
build/libxszlib.a: $(ZLIB_OBJ)
	rm -f $@ && ar rcs $@ $(ZLIB_OBJ)
	rm -rf build/zlib-inc && mkdir -p build/zlib-inc && cp third_party/zlib/zlib.h third_party/zlib/zconf.h build/zlib-inc/

# Brotli 1.2.1 from source (third_party/brotli, MIT): node:zlib's brotliCompress/brotliDecompress, the version Node
# links (so the same bytes). Its own static library, linked only when a program imports node:zlib, and then only
# what is called: the decoder is ~170 KB (the 120 KB dictionary), the encoder ~600 KB more.
BROTLI_SRC := $(wildcard third_party/brotli/common/*.c third_party/brotli/dec/*.c third_party/brotli/enc/*.c)
BROTLI_OBJ := $(patsubst third_party/brotli/%.c,build/brotli/%.o,$(BROTLI_SRC))
build/brotli/%.o: third_party/brotli/%.c
	@mkdir -p $(dir $@)
	$(CC) -O2 -w -Ithird_party/brotli/include -c -o $@ $<
build/libxsbrotli.a: $(BROTLI_OBJ)
	rm -f $@ && ar rcs $@ $(BROTLI_OBJ)

# QBE backend (third_party/qbe, MIT) and the runtime as a linkable object
build/qbe: $(wildcard third_party/qbe/*.c third_party/qbe/*/*.c) | build
	@# config.h names QBE's default target and is generated for the machine that built it first: a tree copied
	@# from another system (a Mac checkout on Linux) would make QBE write the other system's assembly
	@cd third_party/qbe && mv -f config.h config.h.prev 2>/dev/null; $(MAKE) -s config.h; \
	  if ! cmp -s config.h config.h.prev; then rm -f *.o */*.o qbe; fi; rm -f config.h.prev
	$(MAKE) -s -C third_party/qbe qbe
	cp third_party/qbe/qbe $@

# frame pointers: stack traces walk them
build/xisto_rt.o: runtime/xisto_rt.c runtime/xisto_rt.h | build
	$(CC) -std=c11 -O2 -ffp-contract=off -fno-omit-frame-pointer -Wno-unused-function -c -o $@ $<

# the same runtime for --debug --backend=qbe: the debugger agent and the xr_dbg_* entry points (docs/debugger.md)
build/xisto_rt_dbg.o: runtime/xisto_rt.c runtime/xisto_rt.h runtime/xisto_dbg.h runtime/xisto_dbg_json.h | build
	$(CC) -std=c11 -O0 -g -DXS_DEBUG -D_GNU_SOURCE -ffp-contract=off -fno-omit-frame-pointer -Wno-unused-function -c -o $@ $<

build/xisto: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) -lm

build/%.o: src/%.c src/xisto.h src/diag.def | build
	$(CC) $(CFLAGS) -c -o $@ $<

# xisto --version: main.o is rebuilt when the version changes
build/main.o: build/version.stamp
build/main.o: CFLAGS += -DXISTO_VERSION='"$(VERSION)"'
build/version.stamp: FORCE | build
	@echo '$(VERSION)' | cmp -s - $@ || echo '$(VERSION)' > $@
FORCE:

build/checker.o: runtime/xisto_regex.h runtime/xisto_regex_bt.h

# Node's modules written in xisto (runtime/node/NAME.ts), embedded in the compiler as xs_node_NAME_ts
NODE_TS := async_hooks crypto stream path fs_promises url events process http https net dns child_process buffer util querystring string_decoder zlib

build/rt_embed.c: tools/rtsplit.py $(NODE_TS:%=runtime/node/%.ts) packages/xisto/io.ts packages/xisto/bytes.ts packages/xisto/parallel.ts packages/xisto/collections.ts packages/xisto/fractal.ts packages/xisto/random.ts runtime/xisto_regex.h runtime/xisto_regex_bt.h runtime/xisto_rt.h runtime/xisto_net.h runtime/xisto_sqlite.h runtime/xisto_tls.h runtime/xisto_tls_awslc.h runtime/xisto_crypto.h runtime/xisto_zlib.h runtime/xisto_dbg_json.h runtime/xisto_dbg.h | build
	{ echo 'const unsigned char xs_runtime_h[] = {'; xxd -i < runtime/xisto_rt.h; echo ', 0x00 };'; \
	  echo 'const unsigned char xs_runtime_net_h[] = {'; xxd -i < runtime/xisto_net.h; echo ', 0x00 };'; \
	  echo 'const unsigned char xs_runtime_sqlite_h[] = {'; xxd -i < runtime/xisto_sqlite.h; echo ', 0x00 };'; \
	  echo 'const unsigned char xs_runtime_tls_h[] = {'; xxd -i < runtime/xisto_tls.h; echo ', 0x00 };'; \
	  echo 'const unsigned char xs_runtime_tls_awslc_h[] = {'; xxd -i < runtime/xisto_tls_awslc.h; echo ', 0x00 };'; \
	  echo 'const unsigned char xs_runtime_crypto_h[] = {'; xxd -i < runtime/xisto_crypto.h; echo ', 0x00 };'; \
	  echo 'const unsigned char xs_runtime_zlib_h[] = {'; xxd -i < runtime/xisto_zlib.h; echo ', 0x00 };'; \
	  echo 'const unsigned char xs_runtime_regex_h[] = {'; xxd -i < runtime/xisto_regex.h; echo ', 0x00 };'; \
	  echo 'const unsigned char xs_runtime_regex_bt_h[] = {'; xxd -i < runtime/xisto_regex_bt.h; echo ', 0x00 };'; \
	  echo 'const unsigned char xs_runtime_dbg_json_h[] = {'; xxd -i < runtime/xisto_dbg_json.h; echo ', 0x00 };'; \
	  echo 'const unsigned char xs_runtime_dbg_h[] = {'; xxd -i < runtime/xisto_dbg.h; echo ', 0x00 };'; \
	  for n in $(NODE_TS); do echo "const unsigned char xs_node_$${n}_ts[] = {"; xxd -i < runtime/node/$$n.ts; echo ', 0x00 };'; done; \
	  echo 'const unsigned char xs_pkg_io_ts[] = {'; xxd -i < packages/xisto/io.ts; echo ', 0x00 };'; \
	  echo 'const unsigned char xs_pkg_bytes_ts[] = {'; xxd -i < packages/xisto/bytes.ts; echo ', 0x00 };'; \
	  echo 'const unsigned char xs_pkg_parallel_ts[] = {'; xxd -i < packages/xisto/parallel.ts; echo ', 0x00 };'; \
	  echo 'const unsigned char xs_pkg_collections_ts[] = {'; xxd -i < packages/xisto/collections.ts; echo ', 0x00 };'; \
	  echo 'const unsigned char xs_pkg_fractal_ts[] = {'; xxd -i < packages/xisto/fractal.ts; echo ', 0x00 };'; \
	  echo 'const unsigned char xs_pkg_random_ts[] = {'; xxd -i < packages/xisto/random.ts; echo ', 0x00 };'; \
	  for h in rt net sqlite tls tls_awslc crypto zlib regex; do for m in decl impl; do \
	    echo "const unsigned char xs_runtime_$${h}_h_$$m[] = {"; python3 tools/rtsplit.py $$m runtime/xisto_$$h.h | xxd -i; echo ', 0x00 };'; \
	  done; done; } > $@

build/rt_embed.o: build/rt_embed.c
	$(CC) $(CFLAGS) -c -o $@ $<

build:
	mkdir -p build

test: all
	sh tests/run.sh

test-cross: all
	sh tests/cross/run.sh

bench: all
	sh bench/run.sh

bench-http: build/xisto
	sh bench/http/run.sh

clean:
	rm -rf build

# a release archive in dist/ (tools/dist.sh): make dist VERSION=0.1.0
dist: all
	sh tools/dist.sh $(VERSION)

.PHONY: all awslc test bench bench-http clean dist
