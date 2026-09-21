import { afterEach, describe, expect, it, vi } from 'vitest';
import { flushPromises, mount } from '@vue/test-utils';
import ChatView from './ChatView.vue';
import thread from '../test/fixtures/chat.json';

afterEach(() => vi.unstubAllGlobals());

const stubPicture = () => {
  vi.stubGlobal('URL', Object.assign(URL, { createObjectURL: () => 'blob:picture', revokeObjectURL: () => {} }));
};
const okFetch = (answer = {}) => vi.fn(async () => ({ ok: true, status: 200, json: async () => answer, blob: async () => new Blob(['x']) }));

describe('ChatView', () => {
  it('says so when nothing has been written', () => {
    const w = mount(ChatView, { props: { token: 't' } });
    expect(w.text()).toContain('Nothing yet');
    expect(w.findAll('[data-test=message]')).toHaveLength(0);
  });

  it('shows the conversation: the player on one side, the GM on the other, a broadcast with its own look, always as text', async () => {
    stubPicture();
    vi.stubGlobal('fetch', okFetch());
    const w = mount(ChatView, { props: { thread, token: 'good' } });
    await flushPromises();
    const bubbles = w.findAll('[data-test=message]');
    expect(bubbles).toHaveLength(2);
    expect(bubbles[0].classes()).toContain('theirs');
    expect(bubbles[1].classes()).toContain('mine');
    expect(bubbles[1].findAll('.message-text').map((p) => p.text())).toEqual(['Is it a cult mark?', 'Or just graffiti?']);
    expect(bubbles[0].text()).toContain('<b>symbol</b>');
    expect(bubbles[0].find('b').exists()).toBe(false);
    const broadcast = w.get('[data-test=broadcast]');
    expect(broadcast.text()).toContain('The Misty Vale party');
    expect(broadcast.classes()).toContain('broadcast');
    expect(broadcast.findAll('.message-text')).toHaveLength(2);
  });

  it('tells the player which of their messages the GM has read', () => {
    const w = mount(ChatView, { props: { thread, token: 't' } });
    expect(w.findAll('[data-test=seen]')).toHaveLength(1);
    const unseen = mount(ChatView, { props: { thread: { ...thread, gmRead: '' }, token: 't' } });
    expect(unseen.findAll('[data-test=seen]')).toHaveLength(0);
  });

  it('sends what is written, to the GM only, and then asks for the conversation again', async () => {
    const f = okFetch({ message: {} });
    vi.stubGlobal('fetch', f);
    const w = mount(ChatView, { props: { thread, token: 'good-token' } });
    expect(w.get('[data-test=send]').attributes('disabled')).toBeDefined();
    await w.get('textarea').setValue('  The door is trapped.  ');
    await w.get('form').trigger('submit');
    await flushPromises();
    expect(f).toHaveBeenCalledWith('/api/chat', expect.objectContaining({ method: 'POST', headers: expect.objectContaining({ Authorization: 'Bearer good-token' }) }));
    const post = f.mock.calls.find(([url]) => url === '/api/chat'); // (the first call fetched the GM's picture)
    expect(JSON.parse(post[1].body)).toEqual({ text: 'The door is trapped.' });
    expect(w.emitted('sent')).toHaveLength(1);
    expect(w.get('textarea').element.value).toBe('');
  });

  it('shows the server\'s reason when a message is refused, and keeps what was written', async () => {
    vi.stubGlobal('fetch', vi.fn(async () => ({ ok: false, status: 400, json: async () => ({ error: 'Could not send: the text is too long.' }) })));
    const w = mount(ChatView, { props: { thread, token: 't' } });
    await w.get('textarea').setValue('hi');
    await w.get('form').trigger('submit');
    await flushPromises();
    expect(w.get('[data-test=chat-error]').text()).toContain('too long');
    expect(w.get('textarea').element.value).toBe('hi');
    expect(w.emitted('sent')).toBeUndefined();
  });

  it('accepts only real picture types and sizes', async () => {
    const w = mount(ChatView, { props: { thread, token: 't' } });
    const input = w.get('input[type=file]');
    const choose = async (file) => {
      Object.defineProperty(input.element, 'files', { value: [file], configurable: true });
      await input.trigger('change');
    };
    await choose(new File(['<svg/>'], 'x.svg', { type: 'image/svg+xml' }));
    expect(w.get('[data-test=chat-error]').text()).toContain('PNG, JPEG, GIF or WebP');
    const big = new File(['x'], 'big.png', { type: 'image/png' });
    Object.defineProperty(big, 'size', { value: 9 * 1024 * 1024 });
    await choose(big);
    expect(w.get('[data-test=chat-error]').text()).toContain('too big');
    expect(w.find('[data-test=attached]').exists()).toBe(false);
  });

  it('sends a picture as base64, and nothing else if there is no text', async () => {
    const f = okFetch({ message: {} });
    vi.stubGlobal('fetch', f);
    const w = mount(ChatView, { props: { thread, token: 't' } });
    const input = w.get('input[type=file]');
    Object.defineProperty(input.element, 'files', { value: [new File(['pretend'], 'map.png', { type: 'image/png' })], configurable: true });
    await input.trigger('change');
    await vi.waitFor(() => expect(w.find('[data-test=attached]').exists()).toBe(true));
    expect(w.get('[data-test=attached]').text()).toContain('map.png');
    await w.get('form').trigger('submit');
    await flushPromises();
    expect(JSON.parse(f.mock.calls.find(([url]) => url === '/api/chat')[1].body)).toEqual({ image: btoa('pretend') });
    expect(w.find('[data-test=attached]').exists()).toBe(false);
  });
});
