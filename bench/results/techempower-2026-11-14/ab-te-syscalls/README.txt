A/B of the te-syscalls branch on the TechEmpower topology A (servers on the Xeon E5-2686 v4, 2 vCPUs; wrk 4.2.0 on
the Graviton2 over the private network; bench/techempower/run.py, 15 s per level, 2 workers), the sides interleaved
in each round, two rounds.
net/   main 40cdc16 (old) x 891458f (three system calls fewer per request) x actix
net3/  891458f (sys1) x 2f69e7c (one listening socket per worker, sys2) x actix
net4/  2f69e7c (sys2) x a6dd9ed (one-pass head parser, one write for small bodies, accept4, sys3) x actix
Single core, loopback (server on cpu0, wrk -t1 -c64 on cpu1), /json: main 93k req/s, 10.2 us CPU and 6.0 system
calls per request; 891458f 103k, 7.3 us, 2.7; actix 99k, 8.6 us, 2.9 (perf stat, raw_syscalls:sys_enter).
