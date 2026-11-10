// The Node build only (xisto never reads a .d.ts): the names xisto's packages give to Node's types.
import "@nestjs/platform-express";
declare module "@nestjs/platform-express" {
  export type UploadedFileData = Express.Multer.File;
}
