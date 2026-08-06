async function encodeAudio(audioBuffer) {
  await new Promise((resolve) => {
    if (Module.calledRun) resolve();
    else Module.onRuntimeInitialized = resolve;
  });

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
  const envPtr = getEnv();
  Module._free(pcmPtr);
const notes = [];
  for (let i = 0; i < count; i++) {
    const base = (notesPtr >> 2) + i * 6;
    const fundamental  = Module.HEAPF32[base + 0];
    const startFrame   = Module.HEAP32[(notesPtr >> 2) + i * 6 + 1];
    const endFrame     = Module.HEAP32[(notesPtr >> 2) + i * 6 + 2];
    const envPtr     = Module.HEAP32[(notesPtr >> 2) + i * 6 + 3];
    const envelopeLen  = Module.HEAP32[(notesPtr >> 2) + i * 6 + 4];

    const envelope = new Float32Array(envelopeLen);
    for (let j = 0; j < envelopeLen; j++) {
      envelope[j] = Module.HEAPF32[(envPtr >> 2) + j];
    }
    notes.push({ fundamental, startFrame, endFrame, envelope, envelopeLength: envelopeLen });
  }
  Module._free(notesPtr);
  Module._free(envPtr);
  return { sampleRate, notes };
}