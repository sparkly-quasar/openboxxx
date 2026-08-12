/**
 * The operations the user actually performs on a sender group.
 */

import { GmailClient, type GmailFilterSpec } from './gmail';
import { removeCached } from './cache';
import type { SenderGroup } from './group';
import type { UnsubscribeInfo } from './parse';

export type BulkAction = 'archive' | 'trash' | 'markRead';

export interface BulkResult {
  action: BulkAction;
  affected: number;
}

/**
 * Apply a bulk action to a group's messages and reconcile the local cache.
 *
 * Trash uses the TRASH label rather than permanent delete, so everything here
 * is recoverable from Gmail for 30 days.
 */
export async function applyBulkAction(
  client: GmailClient,
  account: string,
  ids: string[],
  action: BulkAction,
  onProgress?: (done: number) => void,
): Promise<BulkResult> {
  if (!ids.length) return { action, affected: 0 };

  switch (action) {
    case 'trash':
      await client.trash(ids, onProgress);
      // Trashed mail is gone from the working set; forget it locally.
      await removeCached(account, ids);
      break;

    case 'archive':
      await client.batchModify(ids, [], ['INBOX']);
      onProgress?.(ids.length);
      break;

    case 'markRead':
      await client.batchModify(ids, [], ['UNREAD']);
      onProgress?.(ids.length);
      break;
  }

  return { action, affected: ids.length };
}

/** What a filter should do with future mail from a sender. */
export interface FilterPlan {
  /** Sender addresses the filter matches. */
  from: string[];
  skipInbox: boolean;
  markRead: boolean;
  /** Name of a label to apply. Created if it doesn't exist. */
  labelName?: string;
  /** Send straight to trash. Mutually exclusive with the rest, in practice. */
  delete: boolean;
}

/**
 * Build the Gmail `from:` criterion for a plan.
 *
 * Gmail accepts a parenthesised OR list, which keeps a domain group's many
 * addresses in a single filter rather than one filter each.
 */
export function buildFromCriterion(addresses: string[]): string {
  const unique = [...new Set(addresses.filter(Boolean))];
  if (unique.length === 0) return '';
  if (unique.length === 1) return unique[0];
  return `{${unique.join(' ')}}`;
}

/**
 * Translate a {@link FilterPlan} into a Gmail filter, creating the target
 * label first if one was named.
 */
export async function createFilterFromPlan(
  client: GmailClient,
  plan: FilterPlan,
): Promise<{ id: string }> {
  const from = buildFromCriterion(plan.from);
  if (!from) throw new Error('A filter needs at least one sender address.');

  const addLabelIds: string[] = [];
  const removeLabelIds: string[] = [];

  if (plan.delete) {
    addLabelIds.push('TRASH');
  } else {
    if (plan.skipInbox) removeLabelIds.push('INBOX');
    if (plan.markRead) removeLabelIds.push('UNREAD');
    if (plan.labelName) {
      const labelId = await ensureLabel(client, plan.labelName);
      addLabelIds.push(labelId);
    }
  }

  if (!addLabelIds.length && !removeLabelIds.length) {
    throw new Error('Pick at least one action for the filter to take.');
  }

  const spec: GmailFilterSpec = {
    criteria: { from },
    action: {
      ...(addLabelIds.length ? { addLabelIds } : {}),
      ...(removeLabelIds.length ? { removeLabelIds } : {}),
    },
  };

  return client.createFilter(spec);
}

/** Find a label by name (case-insensitive), creating it if absent. */
export async function ensureLabel(client: GmailClient, name: string): Promise<string> {
  const wanted = name.trim();
  if (!wanted) throw new Error('Label name cannot be empty.');

  const labels = await client.listLabels();
  const existing = labels.find((l) => l.name.toLowerCase() === wanted.toLowerCase());
  if (existing) return existing.id;

  const created = await client.createLabel(wanted);
  return created.id;
}

export type UnsubscribeOutcome =
  /** RFC 8058 POST was sent. Browsers hide the response, so this is unconfirmed. */
  | { kind: 'one-click-sent' }
  /** Caller should open this URL — it needs the user's browser and eyes. */
  | { kind: 'open-url'; url: string }
  /** Caller should open a mail composer for this address. */
  | { kind: 'mailto'; url: string }
  | { kind: 'unavailable' };

/**
 * Decide how to unsubscribe from a sender.
 *
 * The honest constraint: a web app cannot verify a cross-origin unsubscribe.
 * RFC 8058 one-click POSTs go out in `no-cors` mode, which means the browser
 * sends the request but refuses to show us the response — we know it left, not
 * that it worked. Anything else hands the user a link, because unsubscribe
 * pages routinely require a confirmation click that only a human can give.
 */
export async function unsubscribe(info: UnsubscribeInfo | null): Promise<UnsubscribeOutcome> {
  if (!info) return { kind: 'unavailable' };

  if (info.oneClick && info.http) {
    try {
      await fetch(info.http, {
        method: 'POST',
        mode: 'no-cors',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: 'List-Unsubscribe=One-Click',
      });
      return { kind: 'one-click-sent' };
    } catch {
      // Fall through to handing the user the link.
      return { kind: 'open-url', url: info.http };
    }
  }

  if (info.http) return { kind: 'open-url', url: info.http };
  if (info.mailto) return { kind: 'mailto', url: info.mailto };
  return { kind: 'unavailable' };
}

/** Summarise what a bulk action will do, for the confirmation sheet. */
export function describeAction(action: BulkAction, group: SenderGroup): string {
  const n = group.count.toLocaleString();
  const plural = group.count === 1 ? 'message' : 'messages';
  switch (action) {
    case 'trash':
      return `Move ${n} ${plural} from ${group.name} to Trash. Recoverable in Gmail for 30 days.`;
    case 'archive':
      return `Remove ${n} ${plural} from ${group.name} from your inbox. They stay searchable in All Mail.`;
    case 'markRead':
      return `Mark ${n} ${plural} from ${group.name} as read.`;
  }
}
