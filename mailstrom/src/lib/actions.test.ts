import { describe, expect, it, vi, beforeEach, afterEach } from 'vitest';
import { buildFromCriterion, createFilterFromPlan, unsubscribe, type FilterPlan } from './actions';
import type { GmailClient } from './gmail';

/** Minimal stand-in for GmailClient covering only what the tests exercise. */
function fakeClient(over: Partial<GmailClient> = {}) {
  return {
    listLabels: vi.fn().mockResolvedValue([]),
    createLabel: vi.fn().mockResolvedValue({ id: 'Label_new', name: 'New' }),
    createFilter: vi.fn().mockResolvedValue({ id: 'filter_1' }),
    ...over,
  } as unknown as GmailClient;
}

const basePlan: FilterPlan = {
  from: ['news@acme.com'],
  skipInbox: true,
  markRead: false,
  delete: false,
};

describe('buildFromCriterion', () => {
  it('passes a single address through', () => {
    expect(buildFromCriterion(['a@x.com'])).toBe('a@x.com');
  });

  it('wraps multiple addresses in Gmail OR-list braces', () => {
    expect(buildFromCriterion(['a@x.com', 'b@x.com'])).toBe('{a@x.com b@x.com}');
  });

  it('de-duplicates and drops blanks', () => {
    expect(buildFromCriterion(['a@x.com', 'a@x.com', ''])).toBe('a@x.com');
  });

  it('returns empty for no addresses', () => {
    expect(buildFromCriterion([])).toBe('');
  });
});

describe('createFilterFromPlan', () => {
  it('builds a skip-inbox filter', async () => {
    const client = fakeClient();
    await createFilterFromPlan(client, basePlan);

    expect(client.createFilter).toHaveBeenCalledWith({
      criteria: { from: 'news@acme.com' },
      action: { removeLabelIds: ['INBOX'] },
    });
  });

  it('combines skip-inbox and mark-read', async () => {
    const client = fakeClient();
    await createFilterFromPlan(client, { ...basePlan, markRead: true });

    expect(client.createFilter).toHaveBeenCalledWith({
      criteria: { from: 'news@acme.com' },
      action: { removeLabelIds: ['INBOX', 'UNREAD'] },
    });
  });

  it('reuses an existing label rather than creating a duplicate', async () => {
    const client = fakeClient({
      listLabels: vi.fn().mockResolvedValue([{ id: 'Label_7', name: 'bulk', type: 'user' }]),
    });

    // Case differs from the stored label — Gmail label names are case-insensitive
    // for this purpose, and creating a second one would fail.
    await createFilterFromPlan(client, { ...basePlan, labelName: 'Bulk' });

    expect(client.createLabel).not.toHaveBeenCalled();
    expect(client.createFilter).toHaveBeenCalledWith({
      criteria: { from: 'news@acme.com' },
      action: { addLabelIds: ['Label_7'], removeLabelIds: ['INBOX'] },
    });
  });

  it('creates the label when it does not exist', async () => {
    const client = fakeClient();
    await createFilterFromPlan(client, { ...basePlan, labelName: 'Fresh' });

    expect(client.createLabel).toHaveBeenCalledWith('Fresh');
    expect(client.createFilter).toHaveBeenCalledWith({
      criteria: { from: 'news@acme.com' },
      action: { addLabelIds: ['Label_new'], removeLabelIds: ['INBOX'] },
    });
  });

  it('makes a delete plan trash-only, ignoring the other toggles', async () => {
    const client = fakeClient();
    await createFilterFromPlan(client, {
      ...basePlan,
      delete: true,
      markRead: true,
      labelName: 'Ignored',
    });

    expect(client.createLabel).not.toHaveBeenCalled();
    expect(client.createFilter).toHaveBeenCalledWith({
      criteria: { from: 'news@acme.com' },
      action: { addLabelIds: ['TRASH'] },
    });
  });

  it('rejects a plan with no sender', async () => {
    await expect(createFilterFromPlan(fakeClient(), { ...basePlan, from: [] })).rejects.toThrow(
      /at least one sender/i,
    );
  });

  it('rejects a plan that would do nothing', async () => {
    await expect(
      createFilterFromPlan(fakeClient(), { ...basePlan, skipInbox: false }),
    ).rejects.toThrow(/at least one action/i);
  });
});

describe('unsubscribe', () => {
  const fetchMock = vi.fn();

  beforeEach(() => {
    fetchMock.mockReset();
    vi.stubGlobal('fetch', fetchMock);
  });

  afterEach(() => {
    vi.unstubAllGlobals();
  });

  it('reports unavailable when there is no header', async () => {
    expect(await unsubscribe(null)).toEqual({ kind: 'unavailable' });
  });

  it('POSTs the RFC 8058 body for one-click senders', async () => {
    fetchMock.mockResolvedValue(new Response(null, { status: 200 }));

    const outcome = await unsubscribe({ http: 'https://acme/u', mailto: null, oneClick: true });

    expect(outcome).toEqual({ kind: 'one-click-sent' });
    expect(fetchMock).toHaveBeenCalledWith(
      'https://acme/u',
      expect.objectContaining({ method: 'POST', mode: 'no-cors', body: 'List-Unsubscribe=One-Click' }),
    );
  });

  it('falls back to opening the link when the one-click POST throws', async () => {
    fetchMock.mockRejectedValue(new Error('blocked'));

    expect(await unsubscribe({ http: 'https://acme/u', mailto: null, oneClick: true })).toEqual({
      kind: 'open-url',
      url: 'https://acme/u',
    });
  });

  it('hands back a URL to open when one-click is not offered', async () => {
    expect(await unsubscribe({ http: 'https://acme/u', mailto: null, oneClick: false })).toEqual({
      kind: 'open-url',
      url: 'https://acme/u',
    });
    expect(fetchMock).not.toHaveBeenCalled();
  });

  it('falls back to mailto when that is all the sender gave', async () => {
    expect(await unsubscribe({ http: null, mailto: 'mailto:stop@acme', oneClick: false })).toEqual({
      kind: 'mailto',
      url: 'mailto:stop@acme',
    });
  });
});
