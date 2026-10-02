// A picture file for a board: shrunk (a photo of 4000 pixels is not needed on a canvas) and given as base64 to send. Where the browser cannot draw
// it (or a test), the file is sent as it is.
const MAX_SIDE = 1600;

const readAsDataUrl = (file) => new Promise((resolve, reject) => {
  const reader = new FileReader();
  reader.onload = () => resolve(String(reader.result));
  reader.onerror = () => reject(reader.error ?? new Error('Could not read the picture.'));
  reader.readAsDataURL(file);
});

export async function pictureForBoard(file, maxSide = MAX_SIDE) {
  if (!file || !String(file.type).startsWith('image/')) throw new Error('That is not a picture.');
  const original = await readAsDataUrl(file);
  if (typeof createImageBitmap === 'function' && typeof document !== 'undefined' && file.type !== 'image/gif') {
    try {
      const bitmap = await createImageBitmap(file);
      const scale = Math.min(1, maxSide / Math.max(bitmap.width, bitmap.height));
      const width = Math.max(1, Math.round(bitmap.width * scale));
      const height = Math.max(1, Math.round(bitmap.height * scale));
      const canvas = document.createElement('canvas');
      canvas.width = width;
      canvas.height = height;
      canvas.getContext('2d').drawImage(bitmap, 0, 0, width, height);
      const keepAlpha = file.type === 'image/png' || file.type === 'image/webp';     // pictures with see-through parts stay PNG; photos become JPEG
      const out = canvas.toDataURL(keepAlpha ? 'image/png' : 'image/jpeg', 0.86);
      return { base64: out.split(',')[1], width, height };
    } catch {
      /* the original is sent */
    }
  }
  return { base64: original.split(',')[1], width: 0, height: 0 };
}
