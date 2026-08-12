import type { SenderGroup } from '../lib/group';
import { formatAge, formatSize } from '../lib/parse';

/** Stable per-sender colour so the same sender always looks the same. */
function avatarColor(key: string): string {
  let hash = 0;
  for (let i = 0; i < key.length; i++) hash = (hash * 31 + key.charCodeAt(i)) | 0;
  return `hsl(${Math.abs(hash) % 360} 58% 45%)`;
}

function initial(group: SenderGroup): string {
  const source = group.name?.trim() || group.key;
  const ch = [...source].find((c) => /\p{L}|\p{N}/u.test(c));
  return (ch ?? '?').toUpperCase();
}

export function SenderRow({ group, onSelect }: { group: SenderGroup; onSelect: () => void }) {
  return (
    <li>
      <button className="sender" onClick={onSelect} data-testid="sender-row">
        <span className="avatar" style={{ background: avatarColor(group.key) }} aria-hidden="true">
          {initial(group)}
        </span>

        <span className="sender-main">
          <span className="sender-name">{group.name}</span>
          <span className="sender-sub">
            {group.unsubscribe ? <span className="tag tag-unsub">unsub</span> : null}
            {group.unread > 0 ? <span className="tag">{group.unread} unread</span> : null}
            <span>{group.addresses.length > 1 ? `${group.addresses.length} addresses` : group.key}</span>
          </span>
        </span>

        <span className="sender-count">
          <b>{group.count.toLocaleString()}</b>
          <span>
            {formatSize(group.size)} · {formatAge(group.newest)}
          </span>
        </span>
      </button>
    </li>
  );
}
