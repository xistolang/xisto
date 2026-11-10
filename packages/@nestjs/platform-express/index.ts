// @nestjs/platform-express for xisto: FileInterceptor("file") with @UploadedFile() reads one file of a
// multipart/form-data upload (Multer's field names).
import { FileInterceptorSpec, UploadedFileData } from "@nestjs/core";

/** Multer options (accepted; files are kept in memory, as Multer's memory storage does). */
export class MulterOptions {
  limits?: MulterLimits;
}
export class MulterLimits {
  fileSize?: int;
}

/**
 * Reads the file of the multipart field `field`: pass it to UseInterceptors and take it with @UploadedFile().
 * @example @UseInterceptors(FileInterceptor("file")) upload(@UploadedFile() file: Express.Multer.File)
 */
export function FileInterceptor(field: string, options: MulterOptions = {}): FileInterceptorSpec {
  const s = new FileInterceptorSpec();
  s.field = field;
  return s;
}

export { UploadedFileData } from "@nestjs/core";
