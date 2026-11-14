// setImmediate that queues another does not hold back timers (Node's phases): the timer fires and the loop ends
let stop = false;
let n = 0;
function spin(): void { n++; if (!stop) setImmediate(spin); }
setTimeout(() => { stop = true; console.log("timer fired", n > 0); }, 5);
setImmediate(spin);
