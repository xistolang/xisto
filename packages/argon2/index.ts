// The contract of npm's argon2 (node-argon2) for argon2id with its default parameters:
// hash(plain) -> "$argon2id$v=19$m=65536,t=3,p=4$<salt>$<hash>", verify(digest, plain).
export declare function hash(plain: string): Promise<string>;
export declare function verify(digest: string, plain: string): Promise<boolean>;
