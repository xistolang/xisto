import { Controller, Get, Version, VERSION_NEUTRAL } from "@nestjs/common";

@Controller({ path: "multi", version: ["1", "2"] })
export class MultiController {
  @Get()
  both(): { from: string } {
    return { from: "multi" };
  }
  @Get("only3")
  @Version("3")
  only3(): { from: string } {
    return { from: "only3" };
  }
}

@Controller({ path: "neutral", version: VERSION_NEUTRAL })
export class NeutralController {
  @Get()
  any(): { from: string } {
    return { from: "neutral" };
  }
}

@Controller({ path: "two", version: "2" })
export class TwoController {
  @Get()
  two(): { from: string } {
    return { from: "two" };
  }
}
