CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -D_GNU_SOURCE
SRC     := $(wildcard src/*.c)
OBJ     := $(patsubst src/%.c,build/%.o,$(SRC)) build/rt_embed.o

# TLS backend: AWS-LC by default (TLS 1.3, assembly crypto; needs cmake, downloads a pinned release once).
# make TLS=bearssl skips it: xisto then links BearSSL (pure C, in the tree). Either is linked into a program
# only when it can use https; plain-http programs (TLS at the load balancer) carry no TLS code.
TLS ?= awslc

all: build/xisto build/qbe build/xisto_rt.o build/xisto_rt_dbg.o build/sqlite3.o build/sqlite3.h build/libbearssl.a $(if $(filter awslc,$(TLS)),build/awslc/lib/libssl.a)

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

build/%.o: src/%.c src/xisto.h | build
	$(CC) $(CFLAGS) -c -o $@ $<

build/checker.o: runtime/xisto_regex.h

build/rt_embed.c: tools/rtsplit.py packages/xisto/io.ts packages/xisto/bytes.ts packages/xisto/parallel.ts packages/xisto/collections.ts runtime/xisto_regex.h runtime/xisto_rt.h runtime/xisto_net.h runtime/xisto_sqlite.h runtime/xisto_tls.h runtime/xisto_tls_awslc.h runtime/xisto_crypto.h runtime/xisto_dbg_json.h runtime/xisto_dbg.h | build
	{ echo 'const unsigned char xs_runtime_h[] = {'; xxd -i < runtime/xisto_rt.h; echo ', 0x00 };'; \
	  echo 'const unsigned char xs_runtime_net_h[] = {'; xxd -i < runtime/xisto_net.h; echo ', 0x00 };'; \
	  echo 'const unsigned char xs_runtime_sqlite_h[] = {'; xxd -i < runtime/xisto_sqlite.h; echo ', 0x00 };'; \
	  echo 'const unsigned char xs_runtime_tls_h[] = {'; xxd -i < runtime/xisto_tls.h; echo ', 0x00 };'; \
	  echo 'const unsigned char xs_runtime_tls_awslc_h[] = {'; xxd -i < runtime/xisto_tls_awslc.h; echo ', 0x00 };'; \
	  echo 'const unsigned char xs_runtime_crypto_h[] = {'; xxd -i < runtime/xisto_crypto.h; echo ', 0x00 };'; \
	  echo 'const unsigned char xs_runtime_regex_h[] = {'; xxd -i < runtime/xisto_regex.h; echo ', 0x00 };'; \
	  echo 'const unsigned char xs_runtime_dbg_json_h[] = {'; xxd -i < runtime/xisto_dbg_json.h; echo ', 0x00 };'; \
	  echo 'const unsigned char xs_runtime_dbg_h[] = {'; xxd -i < runtime/xisto_dbg.h; echo ', 0x00 };'; \
	  echo 'const unsigned char xs_pkg_io_ts[] = {'; xxd -i < packages/xisto/io.ts; echo ', 0x00 };'; \
	  echo 'const unsigned char xs_pkg_bytes_ts[] = {'; xxd -i < packages/xisto/bytes.ts; echo ', 0x00 };'; \
	  echo 'const unsigned char xs_pkg_parallel_ts[] = {'; xxd -i < packages/xisto/parallel.ts; echo ', 0x00 };'; \
	  echo 'const unsigned char xs_pkg_collections_ts[] = {'; xxd -i < packages/xisto/collections.ts; echo ', 0x00 };'; \
	  for h in rt net sqlite tls tls_awslc crypto regex; do for m in decl impl; do \
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

.PHONY: all awslc test bench bench-http clean
