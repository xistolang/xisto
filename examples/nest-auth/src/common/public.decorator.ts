import { SetMetadata } from "@nestjs/common";

export const IS_PUBLIC_KEY = "isPublic";
export const Public = () => SetMetadata(IS_PUBLIC_KEY, true);

export const ROLES_KEY = "roles";
export const Roles = (role: string) => SetMetadata(ROLES_KEY, [role]);

export const RAW_KEY = "raw";
export const Raw = () => SetMetadata(RAW_KEY, true);
