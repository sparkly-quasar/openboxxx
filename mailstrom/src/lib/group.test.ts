import { describe, expect, it } from 'vitest';
import { filterGroups, groupMessages, sortGroups, totals } from './group';
import type { MessageMeta } from './parse';

const DAY = 86_400_000;

function msg(over: Partial<MessageMeta> & { id: string; email: string }): MessageMeta {
  return {
    name: over.email,
    domain: over.email.split('@')[1] ?? '',
    subject: '',
    size: 1000,
    date: 1_700_000_000_000,
    unread: false,
    inInbox: true,
    unsubscribe: null,
    ...over,
  };
}

describe('groupMessages by sender', () => {
  it('aggregates counts, size, unread and inbox tallies', () => {
    const groups = groupMessages(
      [
        msg({ id: '1', email: 'a@x.com', size: 100, unread: true }),
        msg({ id: '2', email: 'a@x.com', size: 250, inInbox: false }),
        msg({ id: '3', email: 'b@y.com', size: 500 }),
      ],
      'sender',
    );

    const a = groups.find((g) => g.key === 'a@x.com')!;
    expect(a.count).toBe(2);
    expect(a.size).toBe(350);
    expect(a.unread).toBe(1);
    expect(a.inInbox).toBe(1);
    expect(a.messageIds).toEqual(['1', '2']);
    expect(groups).toHaveLength(2);
  });

  it('tracks the newest and oldest dates', () => {
    const groups = groupMessages(
      [
        msg({ id: '1', email: 'a@x.com', date: 5 * DAY }),
        msg({ id: '2', email: 'a@x.com', date: 1 * DAY }),
        msg({ id: '3', email: 'a@x.com', date: 3 * DAY }),
      ],
      'sender',
    );
    expect(groups[0].newest).toBe(5 * DAY);
    expect(groups[0].oldest).toBe(1 * DAY);
  });

  it('prefers a real display name over a bare address', () => {
    const groups = groupMessages(
      [
        msg({ id: '1', email: 'a@x.com', name: 'a@x.com', date: 9 * DAY }),
        msg({ id: '2', email: 'a@x.com', name: 'Acme', date: 1 * DAY }),
      ],
      'sender',
    );
    // Even though the bare-address message is newer, the real name wins.
    expect(groups[0].name).toBe('Acme');
  });

  it('prefers the most recent among equally real names', () => {
    const groups = groupMessages(
      [
        msg({ id: '1', email: 'a@x.com', name: 'Old Brand', date: 1 * DAY }),
        msg({ id: '2', email: 'a@x.com', name: 'New Brand', date: 9 * DAY }),
      ],
      'sender',
    );
    expect(groups[0].name).toBe('New Brand');
  });

  it('keeps the newest unsubscribe target', () => {
    const groups = groupMessages(
      [
        msg({
          id: '1',
          email: 'a@x.com',
          date: 1 * DAY,
          unsubscribe: { http: 'https://old', mailto: null, oneClick: false },
        }),
        msg({
          id: '2',
          email: 'a@x.com',
          date: 9 * DAY,
          unsubscribe: { http: 'https://new', mailto: null, oneClick: true },
        }),
        msg({ id: '3', email: 'a@x.com', date: 10 * DAY, unsubscribe: null }),
      ],
      'sender',
    );
    expect(groups[0].unsubscribe).toEqual({ http: 'https://new', mailto: null, oneClick: true });
  });

  it('skips messages with no sender address', () => {
    const groups = groupMessages([msg({ id: '1', email: '' })], 'sender');
    expect(groups).toHaveLength(0);
  });
});

describe('groupMessages by domain', () => {
  it('folds distinct addresses on one domain together', () => {
    const groups = groupMessages(
      [
        msg({ id: '1', email: 'news@acme.com' }),
        msg({ id: '2', email: 'billing@acme.com' }),
        msg({ id: '3', email: 'hi@other.com' }),
      ],
      'domain',
    );

    const acme = groups.find((g) => g.key === 'acme.com')!;
    expect(acme.count).toBe(2);
    expect(acme.addresses).toEqual(['billing@acme.com', 'news@acme.com']);
    expect(groups).toHaveLength(2);
  });
});

describe('sortGroups', () => {
  const groups = groupMessages(
    [
      // few@ has one big, recent message; many@ has two small, older ones.
      msg({ id: '1', email: 'few@x.com', size: 9000, date: 9 * DAY }),
      msg({ id: '2', email: 'many@x.com', size: 10, unread: true, date: 2 * DAY }),
      msg({ id: '3', email: 'many@x.com', size: 10, unread: true, date: 3 * DAY }),
    ],
    'sender',
  );

  it('sorts by count', () => {
    expect(sortGroups(groups, 'count')[0].key).toBe('many@x.com');
  });

  it('sorts by size', () => {
    expect(sortGroups(groups, 'size')[0].key).toBe('few@x.com');
  });

  it('sorts by unread', () => {
    expect(sortGroups(groups, 'unread')[0].key).toBe('many@x.com');
  });

  it('sorts by recency', () => {
    expect(sortGroups(groups, 'recent')[0].key).toBe('few@x.com');
  });

  it('does not mutate its input', () => {
    const before = groups.map((g) => g.key);
    sortGroups(groups, 'size');
    expect(groups.map((g) => g.key)).toEqual(before);
  });
});

describe('filterGroups', () => {
  const groups = groupMessages(
    [
      msg({ id: '1', email: 'news@acme.com', name: 'Acme Weekly' }),
      msg({ id: '2', email: 'hello@zeta.io', name: 'Zeta' }),
    ],
    'sender',
  );

  it('matches on display name, case-insensitively', () => {
    expect(filterGroups(groups, 'acme weekly')).toHaveLength(1);
  });

  it('matches on address', () => {
    expect(filterGroups(groups, 'zeta.io')[0].key).toBe('hello@zeta.io');
  });

  it('returns everything for an empty query', () => {
    expect(filterGroups(groups, '  ')).toHaveLength(2);
  });
});

describe('totals', () => {
  it('sums across groups', () => {
    const groups = groupMessages(
      [
        msg({ id: '1', email: 'a@x.com', size: 100, unread: true }),
        msg({
          id: '2',
          email: 'b@x.com',
          size: 200,
          unsubscribe: { http: 'https://u', mailto: null, oneClick: false },
        }),
      ],
      'sender',
    );

    expect(totals(groups)).toEqual({
      senders: 2,
      messages: 2,
      size: 300,
      unread: 1,
      unsubscribable: 1,
    });
  });
});
