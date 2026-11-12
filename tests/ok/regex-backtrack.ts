// RegExp with backtracking (runtime/xisto_regex_bt.h): backreferences, lookahead and lookbehind, named references,
// lazy and counted quantifiers with backreferences, lastIndex with g and y, split/replace/matchAll, new RegExp(text);
// every result as V8's
// build: --tsc-checked
const cpf = /^(\d)\1{10}$/;
console.log(cpf.test('11111111111'), cpf.test('11111111112'), cpf.test('1111111111'));
console.log('abcabc xyzxyz abab'.match(/(\w+)\1/g));
const dup = /\b(\w+)\s+\1\b/gi;
console.log('the The cat sat sat on on the mat'.replace(dup, '$1'));
const nm = /(?<y>\d{4})-(?<m>\d\d)-\k<m>/;
const r = nm.exec('x 2024-05-05 y');
console.log(r?.index, r?.[0], r?.groups?.y, r?.groups?.m);
console.log('price: $42, cost: $7'.match(/(?<=\$)\d+/g));
console.log('foobar foobaz'.replace(/foo(?=bar)/g, 'X'), 'a1b2c3'.replace(/(?<!a)\d/g, '#'));
console.log(/^(?=.*\d)(?=.*[a-z])(?=.*[A-Z]).{8,}$/.test('Passw0rdX'), /^(?=.*\d)(?=.*[a-z])(?=.*[A-Z]).{8,}$/.test('password'));
const g = /(a)|(b)\2/g;
let m: RegExpExecArray | null;
const out: string[] = [];
while ((m = g.exec('abba')) !== null) out.push(`${m.index}:${m[0]}:${g.lastIndex}`);
console.log(out.join(' '));
console.log('aaa'.split(/(?<=a)/), 'x--y'.split(/(?<=-)(?=y)/));
console.log(/(a+)+b/.test('aaab'));
console.log([...'a1b22c333'.matchAll(/(\d)\1*/g)].map((x) => x[0]).join(','));
console.log(new RegExp('(x)\\1', 'g').test('axxb'), new RegExp('(?<=a)b').exec('ab')?.index);
console.log(/(?:(a)|b)\1c/.exec('bc')?.[0], /((a)|b)+\2/.exec('aba')?.[0]);
console.log(/(\w)(?=(\w))/.exec('ab')?.join('|'), /(?!(a))\1b/.exec('xb')?.[0]);
console.log('ÁÉ áé'.replace(/(é)\1?/gi, '<$1>'), /(é)\1/i.test('éÉ'));
const sticky = /(\d)\1/y;
sticky.lastIndex = 2;
console.log(sticky.test('ab11'), sticky.lastIndex, sticky.test('ab11'));
const casos: [RegExp, string][] = [
  [/(a*?)b\1/, 'aabaa'],
  [/(a{1,3})\1/, 'aaaaaa'],
  [/(a{1,3}?)\1/, 'aaaaaa'],
  [/(?<=(a|bc))d/, 'xbcd'],
  [/(?<!^)x/g, 'xaxbx'],
  [/^(?:(x)|y)+\1$/, 'xyx'],
  [/(["'])(.*?)\1/g, `say "hi" and 'bye' "x'`],
    [/<(\w+)[^>]*>(.*?)<\/\1>/g, '<b>x</b><i a="1">y</i><u>z</b>'],
    [/(\d+)(?=px)/g, '10px 20em 30px'],
    [/(?<a>.)(?<b>.)\k<b>\k<a>/, 'xabbay'],
    [/\b(\w)\w*\1\b/gi, 'Anna bob Civic deed level xyz'],
    [/(?=(\d{3}))/g, '12345'],
    [/([a-c])\1{2,}/gi, 'aAaa bbcCCc'],
    [/(.)(?!\1)(.)/u, 'aab'],
    [/^(?:(\w)(?=\w*\1))+$/, 'aa'],
    [/x(?=y(?=z))/, 'xyz xya'],
  ];
  function mostra(m: RegExpMatchArray | null): string {
    if (m === null) return 'null';
    const partes: string[] = [];
    for (let i = 0; i < m.length; i++) partes.push(m[i] ?? 'undef');
    return partes.join('|');
  }
  for (const [re, s] of casos) {
    const m = re.global ? s.match(re) : re.exec(s);
    console.log(re.toString(), mostra(m), re.lastIndex);
  }
  console.log('2024-01-01,2023-12-12'.replace(/(\d+)-(\d+)-\2/g, '<$1|$2>'));
  console.log('aXbXXc'.split(/(?<=X)(?!X)/));
  console.log('abcABC'.replace(/(?<=([a-c]))([A-C])/gi, (_m: string, a: string, b: string) => `${a}${b}`));
