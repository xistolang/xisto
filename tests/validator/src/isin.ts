import { IsIn, validationMessages } from "class-validator";
import { ClassInfo } from "xisto:reflect";

class Q {
  @IsIn([1, 2])
  v: string | number = 0;
  @IsIn(["a", 3])
  w: string | number = "a";
}
const ci: ClassInfo = Q;
function check(v: string | number, w: string | number): void {
  const q = new Q();
  q.v = v;
  q.w = w;
  console.log(JSON.stringify(validationMessages(q, ci.id)));
}
check(1, "a");
check("1", "a");
check(2, 3);
check(3, "3");
