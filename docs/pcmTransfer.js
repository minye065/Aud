const runtimeReady = new Promise((resolve) => {
  Module.onRuntimeInitialized = resolve;
});
async function encodeAudio(audioBuffer) {
  await runtimeReady;

  const samples = audioBuffer.getChannelData(0);
  const sampleRate = audioBuffer.sampleRate;
  const sampleCount = samples.length;
  const pcmPtr = Module._malloc(sampleCount * 4);
  Module.HEAPF32.set(samples, pcmPtr >> 2);
  const encode = Module.cwrap('encode', 'number', ['number', 'number', 'number']);
  const notesPtr = encode(pcmPtr, sampleCount, sampleRate);
  const getCount = Module.cwrap('get_note_count', 'number', []);
  const getEnv = Module.cwrap('get_envelope_ptr', 'number', []);
  const count = getCount();
  Module._free(pcmPtr);

  const notes = [];
  for (let i = 0; i < count; i++) {
    const o = (notesPtr >> 2) + i * 6;
    const fundamental = Module.HEAPF32[o + 0];
    const startFrame = Module.HEAP32[o + 1];
    const endFrame = Module.HEAP32[o + 2];
    const noteEnvPtr = Module.HEAP32[o + 3];
    const envelopeLen = Module.HEAP32[o + 4];

    const envelope = new Float32Array(envelopeLen);
    for (let j = 0; j < envelopeLen; j++) {
      envelope[j] = Module.HEAPF32[(noteEnvPtr >> 2) + j];
    }
    notes.push({ fundamental, startFrame, endFrame, envelope, envelopeLength: envelopeLen });
  }
  Module._free(notesPtr);
  return { count, notes };
}