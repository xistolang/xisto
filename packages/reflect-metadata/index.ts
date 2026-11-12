// reflect-metadata for xisto: the global Reflect's metadata calls (import "reflect-metadata" installs it), with their
// Node semantics: metadata kept per target object and property, read back by the same target (===).
// Compatibility: a target is any value (a class as a value, a function, an object), compared by identity; the
// prototype chain is not walked (getMetadata reads what the target itself has, as getOwnMetadata), since a class
// here has no prototype object to walk. The store is xisto:reflect's, where Nest's SetMetadata writes too.
import { ClassInfo, MethodInfo, __xs_meta_define, __xs_meta_get, __xs_meta_find, __xs_meta_keys, __xs_meta_delete } from "xisto:reflect";

export class Reflect {
  /** Reflect.defineMetadata(key, value, target, propertyKey?): value kept for that target (and property). */
  static defineMetadata(metadataKey: unknown, metadataValue: unknown, target: unknown, propertyKey: unknown = null): void {
    __xs_meta_define(metadataKey, metadataValue, target, propertyKey);
  }

  /** The value under key for the target (and property), or undefined. */
  static getMetadata(metadataKey: unknown, target: unknown, propertyKey: unknown = null): any {
    return __xs_meta_get(metadataKey, target, propertyKey);
  }

  static getOwnMetadata(metadataKey: unknown, target: unknown, propertyKey: unknown = null): any {
    return __xs_meta_get(metadataKey, target, propertyKey);
  }

  static hasMetadata(metadataKey: unknown, target: unknown, propertyKey: unknown = null): boolean {
    return __xs_meta_find(metadataKey, target, propertyKey) >= 0;
  }

  static hasOwnMetadata(metadataKey: unknown, target: unknown, propertyKey: unknown = null): boolean {
    return __xs_meta_find(metadataKey, target, propertyKey) >= 0;
  }

  /** The keys defined for the target (and property), in the order they were first defined. */
  static getMetadataKeys(target: unknown, propertyKey: unknown = null): unknown[] {
    return __xs_meta_keys(target, propertyKey);
  }

  static getOwnMetadataKeys(target: unknown, propertyKey: unknown = null): unknown[] {
    return __xs_meta_keys(target, propertyKey);
  }

  /** Removes the key from the target (and property); whether it was there. */
  static deleteMetadata(metadataKey: unknown, target: unknown, propertyKey: unknown = null): boolean {
    return __xs_meta_delete(metadataKey, target, propertyKey);
  }

  /** @Reflect.metadata(key, value): the decorator form, on a class (its ClassInfo is the target) or a method. */
  static metadata(metadataKey: unknown, metadataValue: unknown): (c: ClassInfo, m: MethodInfo | null) => void {
    return (c: ClassInfo, m: MethodInfo | null): void => {
      __xs_meta_define(metadataKey, metadataValue, c, m === null ? null : m.name);
    };
  }
}
