// import { Worker } from "node:worker_threads" alone (no worker started) compiles and runs
import { Worker, isMainThread } from "node:worker_threads";
console.log("main thread:", isMainThread);
