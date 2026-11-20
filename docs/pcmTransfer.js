const runtimeReady = new Promise((resolve) =>
{
  Module.onRuntimeInitialized = resolve;
});
async function encodeAudio(audioBuffer)
{
  await runtimeReady;

  const channelCount = audioBuffer.numberOfChannels;
  const samples = new Float32Array(audioBuffer.length);
  for (let c = 0; c < channelCount; c++)
  {
    const channelData = audioBuffer.getChannelData(c);
    for (let i = 0; i < samples.length; i++)
    {
      samples[i] += channelData[i] / channelCount;
    }
  }  
  const sampleRate = audioBuffer.sampleRate;
  const sampleCount = samples.length;
  const pcmPtr = Module._malloc(sampleCount * 4);
  Module.HEAPF32.set(samples, pcmPtr >> 2);
  const encode = Module.cwrap('encode', 'number', ['number', 'number', 'number']);
  const notesPtr = encode(pcmPtr, sampleCount, sampleRate);
  if (!notesPtr)
  {
    Module._free(pcmPtr);
    return { noteCount: 0, sampleRate, notes: [] };
  }
  const getCount = Module.cwrap('get_note_count', 'number', []);
  const getEnv = Module.cwrap('get_envelope_ptr', 'number', []);
  const count = getCount();
  Module._free(pcmPtr);

  const notes = [];
  for (let i = 0; i < count; i++)
  {
    const o = (notesPtr >> 2) + i * 6;
    const fundamental = Module.HEAPF32[o + 0];
    const startFrame = Module.HEAP32[o + 1];
    const endFrame = Module.HEAP32[o + 2];
    const noteEnvPtr = Module.HEAP32[o + 3];
    const envelopeLen = Module.HEAP32[o + 4];

    const envelope = new Float32Array(envelopeLen);
    for (let j = 0; j < envelopeLen; j++)
    {
      envelope[j] = Module.HEAPF32[(noteEnvPtr >> 2) + j];
    }
    notes.push({ fundamental, startFrame, endFrame, envelope, envelopeLength: envelopeLen });
  }
  Module._free(notesPtr);
  const cleanupEnvelopes = Module.cwrap('cleanup_envelopes', null, []);
  cleanupEnvelopes();
  const usable = notes.filter(n => n.fundamental > 0);
  let loudest = 0;
  for (const n of usable)
  {
    for (let j = 0; j < n.envelope.length; j++)
    {
      if (n.envelope[j] > loudest) loudest = n.envelope[j];
    }
  }
  if (loudest > 0)
  {
    for (const n of usable)
    {
      for (let j = 0; j < n.envelope.length; j++)
      {
        n.envelope[j] /= loudest;
      }
    }
  }
  return { noteCount: usable.length, sampleRate, notes: usable };
}

function writeFile(result, songName)
{
    const itemBaseHz = 261.63;
    const outputFileLines = [];
    outputFileLines.push(`AUD ${songName} ${result.sampleRate} 1024`);
    outputFileLines.push(`item item0 ${itemBaseHz} 1 adsr:.01,.05,1,.05`);
    for (const currentNote of result.notes)
    {
        let noteEnvelopePeak = 0;
        for (let j = 0; j < currentNote.envelope.length; j++)
        {
            if (currentNote.envelope[j] > noteEnvelopePeak) noteEnvelopePeak = currentNote.envelope[j];
        }
        if (noteEnvelopePeak <= 0) continue;
        const tone = Math.round(1200 * Math.log2(currentNote.fundamental / itemBaseHz));
        outputFileLines.push(`use item0 tone:${tone} frame:${currentNote.startFrame}-${currentNote.endFrame + 1} gain:${noteEnvelopePeak.toFixed(3)}`);
    }
    return outputFileLines.join('\n') + '\n';
}