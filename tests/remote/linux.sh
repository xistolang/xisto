#!/bin/sh
# The Mac's whole suite (make test) on a Linux machine over SSH, so every system runs the same tests:
# the tree and Node's outputs from here (Node there may not run .ts), the build with AWS-LC (the default TLS),
# a Redis on 127.0.0.1:56379 for the redis package test. AWS-LC needs ~2 GB of memory to build: on a smaller
# machine set XISTO_REMOTE_TLS=bearssl (that TLS test is then skipped there).
# Usage: XISTO_REMOTE_KEY=key.pem sh tests/remote/linux.sh user@host [user@host ...]   (run `make test` here first)
cd "$(dirname "$0")/../.." || exit 1
[ $# -gt 0 ] || { echo "usage: sh tests/remote/linux.sh user@host ..."; exit 2; }
ls build/tests/*.node > /dev/null 2>&1 || { echo "run make test first (Node's outputs)"; exit 1; }
KEY=${XISTO_REMOTE_KEY:+-i $XISTO_REMOTE_KEY}
TLS=${XISTO_REMOTE_TLS:-awslc}
W=bench/out/remote; rm -rf "$W"; mkdir -p "$W/tree/oracle"
COPYFILE_DISABLE=1 tar --exclude=./build --exclude=./bench/out --exclude='.xisto' --exclude=./third_party/qbe/qbe --exclude='*.o' \
    --exclude='./third_party/aws-lc/aws-lc-[0-9]*[0-9]' --exclude=./node_modules --exclude='*/node_modules' --exclude=./examples/conduit/dist \
    --exclude=./examples/conduit/conduit --exclude='*.bun-build' -cf - . | (cd "$W/tree" && tar -xf -)
find "$W/tree" -name .xisto -prune -exec rm -rf {} \;
cp build/tests/*.node "$W/tree/oracle/"
(cd "$W/tree" && COPYFILE_DISABLE=1 tar --no-xattrs -czf ../xisto.tgz .)
cat > "$W/remote.sh" <<IN
#!/bin/sh
# on the remote machine
if command -v apt-get > /dev/null; then
  sudo DEBIAN_FRONTEND=noninteractive apt-get update -qq
  sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq build-essential g++ cmake ninja-build perl python3 xxd curl openssl tzdata git nodejs redis-server netcat-openbsd > /dev/null
else
  sudo dnf install -y -q gcc gcc-c++ make cmake ninja-build perl python3 vim-common curl openssl tzdata git nodejs redis6 nmap-ncat tar gzip procps-ng > /dev/null
fi
REDIS=\$(command -v redis-server || command -v redis6-server)
pgrep -f "redis.*56379" > /dev/null || \$REDIS --port 56379 --save '' --appendonly no --daemonize yes > /dev/null
if ! swapon --show | grep -q swapfile && [ \$(free -m | awk '/Mem/{print \$2}') -lt 3000 ]; then
  sudo dd if=/dev/zero of=/swapfile bs=1M count=2048 status=none && sudo chmod 600 /swapfile && sudo mkswap /swapfile > /dev/null && sudo swapon /swapfile
fi
rm -rf ~/xisto && mkdir -p ~/xisto && tar -xzf ~/xisto.tgz -C ~/xisto 2>/dev/null && cd ~/xisto
git config --global user.email test@example.com; git config --global user.name test
echo "== \$(uname -sm), \$(cc --version | head -1), TLS $TLS"
make -s TLS=$TLS > /tmp/xisto-make.log 2>&1 || { tail -20 /tmp/xisto-make.log; exit 1; }
XISTO_ORACLE_DIR=\$HOME/xisto/oracle make -s TLS=$TLS test 2>&1 | grep -E "^(tests:|FAIL|ok: redis|skip)"
IN
for h in "$@"; do
    ( scp $KEY -o BatchMode=yes -q "$W/xisto.tgz" "$W/remote.sh" "$h:" && ssh $KEY -o BatchMode=yes "$h" 'sh remote.sh' > "$W/$h.log" 2>&1
      echo "### $h"; cat "$W/$h.log" ) &
done
wait
