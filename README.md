# Aud
Aud is a two part project made up of a website and a windows app.
The website is done and can be found at https://minye065.github.io/Aud/.
The website allows you to upload files and turn them into a custom format. The format has no use yet but will be used to play audio on the app.

When using site:
DO NOT UPLOAD LARGE FILES IT WILL CAUSE A HEAP OOM - will be fixed soon
THIS IS A PROOF OF CONCEPT 
ONLY UPLOAD MP4 MP3 OR WAV
ANY OTHER FILE WILL BREAK THE SYSTEM

DO NOT UPLOAD A FILE AFTER ANOTHER HAS BEEN PROCCESSED
THIS WILL CAUSE IT TO BREAK - known bug will be fixed soon (has to do with not freeing mem bc i need to send it to the js file)


__ __
Run locally (useless there's nothing to run locally yet)
Pre reqs: git, github acc w/ pages access

Clone this project
```
	git clone https://github.com/minye065/Aud
```

Install emcc
```
	git clone https://github.com/emscripten-core/emsdk.git
	cd emsdk
	emsdk install latest
	emsdk activate latest
 ```

 Env var
 ```
	C:\dev\emsdk\emsdk_env.ps1
 ```

 Compile
 ```
	emcc web_encoder.c kissfft/kiss_fft.c kissfft/kiss_fftr.c -Ikissfft -O0 -s ALLOW_MEMORY_GROWTH=1 -s EXPORTED_FUNCTIONS="['_encode','_get_note_count','_get_envelope_ptr','_malloc','_free']" -s EXPORTED_RUNTIME_METHODS="['cwrap','HEAPF32','HEAP32']" -o encoder.js
 ```

 Push to git 
 ```
	git add .
    git commit -m "update encoder"
	git push
 ```
-- --
Below is a rough explanation of the things used in the project.

Another note, this project is made with the goal of having as few outside libraries as possible

 Sound is a series of vibrations
 More accurately it is the change in pressure of a medium

 Sound can be represented by a few variables
 Frequency or pitch
 Amplitude
 Timbre
 Duration
 Envelope

 Pitch is the speed at which the sound wave vibrates or the amount of sound waves pass through each second, measured in hertz or Hz
 Amplitude, more commonly reffered to as volume, can be measured as the distance between the trough and peak of a wave (peak-to-peak amplitude) or as center baseline to peak (peak amplitude), measured in decibals or dB
 Timbre or tone quality describes the sound of a note, this is just the differenes in two complex sound waves
 Duration is the time of a sound event (yes thats it)
 Envelope describes the a single sound event's dynamics in four metrics
	
 Attack
 Decay
 Sustain
 Release

 Attack is the time it takes from silence to maximum volume
 Decay is the time it takes from maximum volume to sustain level
 Sustain is the volume level when a sound event is being maintained
 Release is the time it takes for sound to fade from the sustain level to silence

 The more mathy part

 Wavelength
 The physical distance between two consecutive peaks
 It is inversely proportional to frequency
 v=fλ
 velocity = frequency * lambda
 lambda represents wavelength

 Probably dont need to know this but basic waveforms
 Sine wave
 Square wave
 Triangle wave
 Sawtooth wave


 Sine waves produce a pure tone
 Square waves instantly switch from one tone to other, they sound buzzy
 Triangle waves produce a tone in between square and sine waves
 Sawtooth waves are sharp and buzz a little

 Pulse-code modulation or PCM is a digital representation of sound
 It takes snapshots of the sound wave at regular intervals


 -- Encoder --
 Upload MP4
 MP4 -> PCM
 PCM -> Sectioned into frames 2048 large with 50% overlap -> Frames
 Frames -> Windowed with hann window formula thing -> Windowed frames
 Windowed frames -> FFT -> smth

 *Frames are held using malloc due to potential stack overflow crashes from large audio files

 We use kissFFT as i am NOT writting a working fft library myself
  -- / --


emcc web_encoder.c kissfft/kiss_fft.c kissfft/kiss_fftr.c -Ikissfft -O0 -s EXPORTED_FUNCTIONS="['_encode','_get_note_count','_get_envelope_ptr','_malloc','_free']" -s EXPORTED_RUNTIME_METHODS="['cwrap','HEAP32','HEAPF32']" -s ASSERTIONS=1 -s STACK_SIZE=16777216 -s ALLOW_MEMORY_GROWTH=1 -o encoder.js