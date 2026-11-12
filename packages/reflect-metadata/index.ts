// reflect-metadata for xisto: the global Reflect's metadata calls (import "reflect-metadata" installs it), with their
// Node semantics: metadata kept per target object and property, read back by the same target (===).
// Compatibility: a target is any value (a class as a value, a function, an object), compared by identity; the
// prototype chain is not walked (getMetadata reads what the target itself has, as getOwnMetadata), since a class
// here has no prototype object to walk; decorators Nest writes keep their own store (@nestjs/common's Reflector).
import { ClassInfo, MethodInfo } from "xisto:reflect";

interface Entry {
  target: unknown;
  prop: unknown;
  key: unknown;
  value: unknown;
}

const entries: Entry[] = [];

function find(key: unknown, target: unknown, prop: unknown): int {
  for (let i = 0; i < entries.length; i++) {
    const e = entries[i];
    if (e.target === target && e.prop === prop && e.key === key) {
      return i;
    }
  }
  return -1;
}

export class Reflect {
  /** Reflect.defineMetadata(key, value, target, propertyKey?): value kept for that target (and property). */
  static defineMetadata(metadataKey: unknown, metadataValue: unknown, target: unknown, propertyKey: unknown = null): void {
    const i = find(metadataKey, target, propertyKey);
    if (i >= 0) {
      entries[i].value = metadataValue;
      return;
    }
    entries.push({ target: target, prop: propertyKey, key: metadataKey, value: metadataValue });
  }

  /** The value under key for the target (and property), or undefined. */
  static getMetadata(metadataKey: unknown, target: unknown, propertyKey: unknown = null): any {
    const i = find(metadataKey, target, propertyKey);
    return i >= 0 ? entries[i].value : null;
  }

  static getOwnMetadata(metadataKey: unknown, target: unknown, propertyKey: unknown = null): any {
    const i = find(metadataKey, target, propertyKey);
    return i >= 0 ? entries[i].value : null;
  }

  static hasMetadata(metadataKey: unknown, target: unknown, propertyKey: unknown = null): boolean {
    return find(metadataKey, target, propertyKey) >= 0;
  }

  static hasOwnMetadata(metadataKey: unknown, target: unknown, propertyKey: unknown = null): boolean {
    return find(metadataKey, target, propertyKey) >= 0;
  }

  /** The keys defined for the target (and property), in the order they were first defined. */
  static getMetadataKeys(target: unknown, propertyKey: unknown = null): unknown[] {
    const keys: unknown[] = [];
    for (const e of entries) {
      if (e.target === target && e.prop === propertyKey) {
        keys.push(e.key);
      }
    }
    return keys;
  }

  static getOwnMetadataKeys(target: unknown, propertyKey: unknown = null): unknown[] {
    return Reflect.getMetadataKeys(target, propertyKey);
  }

  /** Removes the key from the target (and property); whether it was there. */
  static deleteMetadata(metadataKey: unknown, target: unknown, propertyKey: unknown = null): boolean {
    const i = find(metadataKey, target, propertyKey);
    if (i < 0) {
      return false;
    }
    entries.splice(i, 1);
    return true;
  }

  /** @Reflect.metadata(key, value): the decorator form, on a class (its ClassInfo is the target) or a method. */
  static metadata(metadataKey: unknown, metadataValue: unknown): (c: ClassInfo, m: MethodInfo | null) => void {
    return (c: ClassInfo, m: MethodInfo | null): void => {
      Reflect.defineMetadata(metadataKey, metadataValue, c, m === null ? null : m.name);
    };
  }
}
