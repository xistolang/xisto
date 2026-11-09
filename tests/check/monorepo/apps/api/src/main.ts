import { Circle } from '@lib/shapes';
import { twice } from '@app/util';
import { Order } from './dto';
import { label } from './helper.js';
import { join } from 'path';
import { connect } from 'some-orm';

const note = "copied from 'no link' in a sentence";

function slug(value: string | null): string {
  return value == null ? '' : value.replace(/[\r\n]/g, '_');
}

function size(o: Order | null): number {
  return o?.id ?? 0;
}

console.log(new Circle(2).area(), twice(3), label(4), slug('/a'), size(null), join('a', 'b'), note, connect());
