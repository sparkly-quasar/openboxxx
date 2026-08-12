import { describe, expect, it } from 'vitest';
import { chunked, GmailError, parseBatchResponse } from './gmail';

/** Build a batch response body the way Gmail formats one. */
function batchBody(parts: { status: number; body: string }[], boundary = 'batch_abc'): string {
  const chunks = parts.map(
    (p, i) =>
      `--${boundary}\r\n` +
      'Content-Type: application/http\r\n' +
      `Content-ID: <response-item-${i}>\r\n\r\n` +
      `HTTP/1.1 ${p.status} ${p.status === 200 ? 'OK' : 'Error'}\r\n` +
      'Content-Type: application/json; charset=UTF-8\r\n\r\n' +
      `${p.body}\r\n`,
  );
  return `${chunks.join('')}--${boundary}--\r\n`;
}

describe('parseBatchResponse', () => {
  it('extracts the JSON body of each successful part', () => {
    const text = batchBody([
      { status: 200, body: JSON.stringify({ id: 'a', sizeEstimate: 10 }) },
      { status: 200, body: JSON.stringify({ id: 'b', sizeEstimate: 20 }) },
    ]);

    const messages = parseBatchResponse(text);
    expect(messages.map((m) => m.id)).toEqual(['a', 'b']);
    expect(messages[1].sizeEstimate).toBe(20);
  });

  it('skips parts that failed, keeping the rest', () => {
    // A message deleted mid-scan 404s; that must not sink its neighbours.
    const text = batchBody([
      { status: 200, body: JSON.stringify({ id: 'a' }) },
      { status: 404, body: JSON.stringify({ error: { message: 'Not Found' } }) },
      { status: 200, body: JSON.stringify({ id: 'c' }) },
    ]);

    expect(parseBatchResponse(text).map((m) => m.id)).toEqual(['a', 'c']);
  });

  it('escalates an auth failure inside a part', () => {
    // A dead token fails every subsequent request, so it must not be swallowed.
    const text = batchBody([
      { status: 200, body: JSON.stringify({ id: 'a' }) },
      { status: 401, body: JSON.stringify({ error: { message: 'Invalid Credentials' } }) },
    ]);

    expect(() => parseBatchResponse(text)).toThrow(GmailError);
  });

  it('ignores parts whose body is not valid JSON', () => {
    const text = batchBody([
      { status: 200, body: '{ truncated' },
      { status: 200, body: JSON.stringify({ id: 'good' }) },
    ]);

    expect(parseBatchResponse(text).map((m) => m.id)).toEqual(['good']);
  });

  it('drops successful parts that carry no id', () => {
    const text = batchBody([{ status: 200, body: JSON.stringify({ sizeEstimate: 1 }) }]);
    expect(parseBatchResponse(text)).toEqual([]);
  });

  it('throws when the payload has no boundary at all', () => {
    expect(() => parseBatchResponse('not a multipart body')).toThrow(/boundary/i);
  });

  it('handles bare-LF line endings', () => {
    const text =
      '--b1\n' +
      'Content-Type: application/http\n\n' +
      'HTTP/1.1 200 OK\n' +
      'Content-Type: application/json\n\n' +
      '{"id":"lf"}\n' +
      '--b1--\n';

    expect(parseBatchResponse(text).map((m) => m.id)).toEqual(['lf']);
  });
});

describe('GmailError', () => {
  it('classifies auth failures', () => {
    expect(new GmailError('x', 401).isAuthError).toBe(true);
    expect(new GmailError('x', 403).isAuthError).toBe(true);
    expect(new GmailError('x', 429).isAuthError).toBe(false);
  });
});

describe('chunked', () => {
  it('splits into fixed-size chunks', () => {
    expect(chunked([1, 2, 3, 4, 5], 2)).toEqual([[1, 2], [3, 4], [5]]);
  });

  it('returns nothing for an empty list', () => {
    expect(chunked([], 10)).toEqual([]);
  });
});
