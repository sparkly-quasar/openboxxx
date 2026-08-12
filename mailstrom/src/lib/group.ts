/**
 * Turning a flat list of message metadata into the sender buckets the UI works
 * on. Pure functions, unit tested.
 */

import type { MessageMeta, UnsubscribeInfo } from './parse';

export type GroupBy = 'sender' | 'domain';
export type SortBy = 'count' | 'size' | 'recent' | 'unread';

export interface SenderGroup {
  /** Grouping key: the sender address, or the domain in domain mode. */
  key: string;
  /** Best display name seen for this group. */
  name: string;
  domain: string;
  /** Distinct sender addresses folded into this group (>1 only in domain mode). */
  addresses: string[];
  messageIds: string[];
  count: number;
  /** Summed Gmail size estimates, bytes. */
  size: number;
  unread: number;
  inInbox: number;
  newest: number;
  oldest: number;
  /** Unsubscribe target from the most recent message that advertised one. */
  unsubscribe: UnsubscribeInfo | null;
}

/**
 * Bucket messages by sender address or sender domain.
 *
 * Display name selection: we keep the name attached to the *most recent*
 * message, since senders rename themselves over time and the latest is the one
 * the user will recognise. A name equal to the raw address loses to any real
 * name, so a group never shows `noreply@x.com` when `X Newsletter` was
 * available.
 */
export function groupMessages(messages: MessageMeta[], by: GroupBy): SenderGroup[] {
  const groups = new Map<string, SenderGroup>();
  // Date of the message that supplied each group's current display name.
  const nameDate = new Map<string, number>();
  // Date of the message that supplied each group's current unsubscribe target.
  const unsubDate = new Map<string, number>();
  const addressSets = new Map<string, Set<string>>();

  for (const m of messages) {
    const key = by === 'domain' ? m.domain || m.email : m.email;
    if (!key) continue;

    let g = groups.get(key);
    if (!g) {
      g = {
        key,
        name: by === 'domain' ? key : m.name,
        domain: m.domain,
        addresses: [],
        messageIds: [],
        count: 0,
        size: 0,
        unread: 0,
        inInbox: 0,
        newest: m.date,
        oldest: m.date,
        unsubscribe: null,
      };
      groups.set(key, g);
      nameDate.set(key, -1);
      unsubDate.set(key, -1);
      addressSets.set(key, new Set());
    }

    g.messageIds.push(m.id);
    g.count++;
    g.size += m.size;
    if (m.unread) g.unread++;
    if (m.inInbox) g.inInbox++;
    if (m.date > g.newest) g.newest = m.date;
    if (m.date < g.oldest) g.oldest = m.date;
    addressSets.get(key)!.add(m.email);

    // Prefer a real display name over a bare address, and among equally "real"
    // candidates prefer the most recent.
    if (by === 'sender') {
      const candidateIsReal = Boolean(m.name) && m.name !== m.email;
      const currentIsReal = Boolean(g.name) && g.name !== key;
      const upgrades = candidateIsReal && !currentIsReal;
      const isNewerPeer = candidateIsReal === currentIsReal && m.date > nameDate.get(key)!;
      if (upgrades || isNewerPeer) {
        g.name = m.name;
        nameDate.set(key, m.date);
      }
    }

    // Keep the newest unsubscribe target — old ones rot.
    if (m.unsubscribe && m.date >= unsubDate.get(key)!) {
      g.unsubscribe = m.unsubscribe;
      unsubDate.set(key, m.date);
    }
  }

  for (const [key, set] of addressSets) {
    const g = groups.get(key)!;
    g.addresses = [...set].sort();
    if (!g.name) g.name = key;
  }

  return [...groups.values()];
}

/** Sort groups in place-safe fashion (returns a new array). */
export function sortGroups(groups: SenderGroup[], by: SortBy): SenderGroup[] {
  const sorted = [...groups];
  switch (by) {
    case 'size':
      sorted.sort((a, b) => b.size - a.size || b.count - a.count);
      break;
    case 'recent':
      sorted.sort((a, b) => b.newest - a.newest);
      break;
    case 'unread':
      sorted.sort((a, b) => b.unread - a.unread || b.count - a.count);
      break;
    case 'count':
    default:
      sorted.sort((a, b) => b.count - a.count || b.size - a.size);
      break;
  }
  return sorted;
}

/** Case-insensitive substring match over name, key and folded addresses. */
export function filterGroups(groups: SenderGroup[], query: string): SenderGroup[] {
  const q = query.trim().toLowerCase();
  if (!q) return groups;
  return groups.filter(
    (g) =>
      g.name.toLowerCase().includes(q) ||
      g.key.toLowerCase().includes(q) ||
      g.addresses.some((a) => a.includes(q)),
  );
}

export interface Totals {
  senders: number;
  messages: number;
  size: number;
  unread: number;
  unsubscribable: number;
}

export function totals(groups: SenderGroup[]): Totals {
  return groups.reduce<Totals>(
    (acc, g) => ({
      senders: acc.senders + 1,
      messages: acc.messages + g.count,
      size: acc.size + g.size,
      unread: acc.unread + g.unread,
      unsubscribable: acc.unsubscribable + (g.unsubscribe ? 1 : 0),
    }),
    { senders: 0, messages: 0, size: 0, unread: 0, unsubscribable: 0 },
  );
}
