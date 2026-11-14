// a once SIGUSR2 listener: after it ran, the next SIGUSR2 has its default action again (the process ends by it, as in Node)
process.once("SIGUSR2", () => {
  console.log("once ran");
  setTimeout(() => {
    console.log("sending again");
    process.kill(process.pid, "SIGUSR2");
    setTimeout(() => console.log("still alive: wrong"), 200);
  }, 20);
});
process.kill(process.pid, "SIGUSR2");
const keep = setTimeout(() => console.log("keepalive ended"), 400);
