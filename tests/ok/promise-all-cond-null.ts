// Promise.all([c ? p : null, ...]): p's value or null
interface Feed { feedUrl: string; expiresAt: string }
class FeedSvc {
  async getReadAccess(): Promise<Feed | null> { return { feedUrl: "https://f", expiresAt: "t" }; }
}
class RolloutSvc {
  async resolveTargetVersion(m: string): Promise<string | null> { if (m === "bad") throw new Error("x"); return "1.2." + m; }
}
async function config(feed: FeedSvc | null, roll: RolloutSvc | null, m: string): Promise<string> {
  const [targetVersion, feedAccess] = await Promise.all([
    roll ? roll.resolveTargetVersion(m).catch(() => null) : null,
    feed ? feed.getReadAccess().catch(() => null) : null,
  ]);
  return `${targetVersion ?? "none"} ${feedAccess ? feedAccess.feedUrl : "nofeed"}`;
}
async function main(): Promise<void> {
  console.log(await config(new FeedSvc(), new RolloutSvc(), "3"));
  console.log(await config(null, new RolloutSvc(), "bad"));
  console.log(await config(new FeedSvc(), null, "x"));
}
main();
