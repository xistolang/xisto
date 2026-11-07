// The original typed the update body as CreateArticleDto and merged it with Object.assign:
// every field is optional here, and only the ones present change.
export class UpdateArticleDto {
  readonly title?: string;
  readonly description?: string;
  readonly body?: string;
  readonly tagList?: string[];
}
