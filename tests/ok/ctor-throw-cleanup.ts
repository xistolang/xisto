// an exception in a constructor releases the object and the fields set so far (the program catches it and goes on)
class Box {
  readonly items: string[] = ["a", "b"];
  readonly name: string;
  constructor(name: string, fail: boolean) {
    this.name = name + "!";
    if (fail) throw new Error("no box for " + name);
  }
}
class Crate extends Box {
  readonly extra: number[] = [1, 2, 3];
  constructor(name: string, fail: boolean) {
    super(name, false);
    if (fail) throw new Error("no crate for " + name);
  }
}
class Early {
  readonly tag: string;
  constructor(n: int) {
    this.tag = `t${n}`;
    if (n === 0) return;
    if (n < 0) throw new Error(`negative ${n}`);
  }
}
for (const fail of [false, true]) {
  try { console.log(new Box("x", fail).name); } catch (e) { console.log((e as Error).message); }
  try { console.log(new Crate("y", fail).extra.length); } catch (e) { console.log((e as Error).message); }
}
for (const n of [0, 1, -1] as int[]) {
  try { console.log(new Early(n).tag); } catch (e) { console.log((e as Error).message); }
}
