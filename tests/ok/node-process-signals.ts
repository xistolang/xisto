// process.on(signal): a listener replaces the default action; process.kill sends one to this process
const onUsr2 = (sig: string): void => {
  console.log("got", sig, process.listenerCount("SIGUSR2"));
  process.off("SIGUSR2", onUsr2);
  console.log("removed", process.listenerCount("SIGUSR2"));
};
process.on("SIGUSR2", onUsr2);
process.once("SIGUSR1", (sig: string) => console.log("once", sig));
console.log(process.kill(process.pid, "SIGUSR2"));
setTimeout(() => {
  process.kill(process.pid, "SIGUSR1");
}, 20);
setTimeout(() => console.log("after", process.listenerCount("SIGUSR1")), 60);
process.on("warning", (w: Error) => console.log("warning event:", w.message));
console.log(typeof process.title, process.getuid() >= 0, process.umask() >= 0);
try {
  process.chdir("/definitely/not/here");
} catch (err) {
  console.log((err as Error).message.startsWith("ENOENT: no such file or directory, chdir"));
}
const here = process.cwd();
process.chdir("/");
console.log(process.cwd());
process.chdir(here);
console.log(process.cwd() === here);
