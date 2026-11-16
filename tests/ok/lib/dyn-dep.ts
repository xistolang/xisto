console.log("dep init");
export let counter: number = 0;
export function bump(): number {
  counter++;
  return counter;
}
