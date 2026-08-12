import { describe, expect, it } from 'vitest';
import {
  domainOf,
  formatAge,
  formatSize,
  header,
  parseFrom,
  parseUnsubscribe,
  toMeta,
} from './parse';

describe('parseFrom', () => {
  it('splits a display name from an address', () => {
    expect(parseFrom('Acme News <news@acme.com>')).toEqual({
      email: 'news@acme.com',
      name: 'Acme News',
    });
  });

  it('unquotes names containing commas', () => {
    expect(parseFrom('"Doe, Jane" <jane@x.io>')).toEqual({
      email: 'jane@x.io',
      name: 'Doe, Jane',
    });
  });

  it('unescapes backslash escapes inside quoted names', () => {
    expect(parseFrom('"Say \\"hi\\"" <a@b.c>').name).toBe('Say "hi"');
  });

  it('handles a bare address', () => {
    expect(parseFrom('bare@example.com')).toEqual({
      email: 'bare@example.com',
      name: 'bare@example.com',
    });
  });

  it('handles an angle-bracketed address with no name', () => {
    expect(parseFrom('<only@example.com>')).toEqual({
      email: 'only@example.com',
      name: 'only@example.com',
    });
  });

  it('lower-cases the address but preserves name casing', () => {
    expect(parseFrom('Big Name <MiXeD@CASE.CoM>')).toEqual({
      email: 'mixed@case.com',
      name: 'Big Name',
    });
  });

  it('picks the last angle group when the name itself contains one', () => {
    expect(parseFrom('weird <fake> <real@x.com>').email).toBe('real@x.com');
  });

  it('returns empties for a blank header', () => {
    expect(parseFrom('')).toEqual({ email: '', name: '' });
  });
});

describe('domainOf', () => {
  it('extracts the domain', () => {
    expect(domainOf('a@b.co.uk')).toBe('b.co.uk');
  });

  it('returns empty when there is no @', () => {
    expect(domainOf('nope')).toBe('');
  });
});

describe('header', () => {
  const headers = [
    { name: 'From', value: 'a@b.c' },
    { name: 'list-unsubscribe', value: '<https://x/u>' },
  ];

  it('is case-insensitive', () => {
    expect(header(headers, 'LIST-UNSUBSCRIBE')).toBe('<https://x/u>');
    expect(header(headers, 'from')).toBe('a@b.c');
  });

  it('returns empty for a missing header', () => {
    expect(header(headers, 'Subject')).toBe('');
    expect(header(undefined, 'From')).toBe('');
  });
});

describe('parseUnsubscribe', () => {
  it('returns null when the header is absent', () => {
    expect(parseUnsubscribe('')).toBeNull();
  });

  it('picks out both https and mailto targets', () => {
    const info = parseUnsubscribe('<https://acme.com/u?id=1>, <mailto:stop@acme.com>');
    expect(info).toEqual({ http: 'https://acme.com/u?id=1', mailto: 'mailto:stop@acme.com', oneClick: false });
  });

  it('flags RFC 8058 one-click', () => {
    const info = parseUnsubscribe('<https://acme.com/u>', 'List-Unsubscribe=One-Click');
    expect(info?.oneClick).toBe(true);
  });

  it('does not flag one-click without an https target', () => {
    const info = parseUnsubscribe('<mailto:stop@acme.com>', 'List-Unsubscribe=One-Click');
    expect(info?.oneClick).toBe(false);
  });

  it('ignores plain http (non-TLS) targets', () => {
    expect(parseUnsubscribe('<http://insecure.example/u>')).toBeNull();
  });

  it('tolerates a missing angle bracket', () => {
    expect(parseUnsubscribe('https://acme.com/u')?.http).toBe('https://acme.com/u');
  });
});

describe('toMeta', () => {
  it('reduces a raw message', () => {
    const meta = toMeta({
      id: 'm1',
      labelIds: ['INBOX', 'UNREAD'],
      sizeEstimate: 2048,
      internalDate: '1700000000000',
      payload: {
        headers: [
          { name: 'From', value: 'Acme <news@acme.com>' },
          { name: 'Subject', value: 'Sale' },
          { name: 'List-Unsubscribe', value: '<https://acme.com/u>' },
          { name: 'List-Unsubscribe-Post', value: 'List-Unsubscribe=One-Click' },
        ],
      },
    });

    expect(meta).toMatchObject({
      id: 'm1',
      email: 'news@acme.com',
      name: 'Acme',
      domain: 'acme.com',
      subject: 'Sale',
      size: 2048,
      date: 1700000000000,
      unread: true,
      inInbox: true,
    });
    expect(meta.unsubscribe?.oneClick).toBe(true);
  });

  it('defaults missing fields rather than throwing', () => {
    const meta = toMeta({ id: 'm2' });
    expect(meta).toMatchObject({ id: 'm2', size: 0, date: 0, unread: false, inInbox: false });
    expect(meta.unsubscribe).toBeNull();
  });
});

describe('formatSize', () => {
  it.each([
    [512, '512 B'],
    [1024, '1 KB'],
    [1536, '1.5 KB'],
    [1024 * 1024, '1 MB'],
    [1024 * 1024 * 1024 * 3.5, '3.5 GB'],
  ])('formats %i as %s', (bytes, expected) => {
    expect(formatSize(bytes)).toBe(expected);
  });
});

describe('formatAge', () => {
  const now = Date.parse('2026-01-01T00:00:00Z');
  const ago = (ms: number) => formatAge(now - ms, now);

  it('covers each bucket', () => {
    expect(ago(30_000)).toBe('now');
    expect(ago(5 * 60_000)).toBe('5m');
    expect(ago(3 * 3_600_000)).toBe('3h');
    expect(ago(4 * 86_400_000)).toBe('4d');
    expect(ago(90 * 86_400_000)).toBe('2mo');
    expect(ago(800 * 86_400_000)).toBe('2y');
  });

  it('does not produce negative ages for clock skew', () => {
    expect(formatAge(now + 60_000, now)).toBe('now');
  });

  it('never floors a bucket to zero', () => {
    // 30 days is not yet a whole month, so it must stay in days rather than
    // rendering as "0mo"; likewise 340 days must not become "0y".
    expect(ago(30 * 86_400_000)).toBe('30d');
    expect(ago(340 * 86_400_000)).toBe('11mo');

    // No age at any point in the first five years should read as zero.
    for (let days = 0; days < 1826; days++) {
      expect(ago(days * 86_400_000)).not.toMatch(/^0(d|mo|y)$/);
    }
  });
});
