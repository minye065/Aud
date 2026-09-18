# Aud
![Aud](https://cdn.hackclub.com/01a08c5f-7b59-73fb-ad83-d0eafa050ddd/image.png)


Aud is a two part project made up of a website and a windows app.
The website is done and can be found at https://minye065.github.io/Aud/.
The website allows you to upload files and turn them into a custom format. The app allows you to play the audio.

The goal is to create a audio player system that has easily shareable files with as few outside libraries as possible
This first version is meant for me to understand the basics of audio and how this would work. 
V2 will focus on how to get this to have small shareable files/text that can still play sound with loss.
__ __
Pre reqs: git, github acc w/ pages access
FOR WINDOWS:
If you don't have git:
```
winget install --id Git.Git -e --source winget
```
FOR OTHER OS:
please check https://git-scm.com/install/mac

cd into your development folder if you have one

If you do not care do this in documents or something:
Press ctrl + r and type `cmd`
``` cd documents```

Clone the project
```
	git clone https://github.com/minye065/Aud
	cd aud
```

For the website:
Install emcc
```
	git clone https://github.com/emscripten-core/emsdk.git
	cd emsdk
	emsdk install latest
	emsdk activate latest
 ```

 Env var
 ```
	.\emsdk_env.ps1
 ```

 Compile
 ```
	cd ../docs
	emcc web_encoder.c kissfft/kiss_fft.c kissfft/kiss_fftr.c -Ikissfft -O0 -s ALLOW_MEMORY_GROWTH=1 -s EXPORTED_FUNCTIONS="['_encode','_get_note_count','_get_envelope_ptr','_cleanup_envelopes','_malloc','_free']" -s EXPORTED_RUNTIME_METHODS="['cwrap','HEAPF32','HEAP32']" -o encoder.js
 ```

 Push to git 
 ```
	git add .
    git commit -m "update encoder"
	git push
 ```

Then the app
```cd ..\src
.\build.bat
```

Tech stack
Uses kissfft which is a "A mixed-radix Fast Fourier Transform"
Uses window app sdk for the app part.
Uses emcc as a compiler for the c part of the website

Learnings:
Win sdk is very bad for ui
fft very annoying to self write (bad idea)
How audio works, deleted bc people think it looks ai (i wrote it myself)




Demo?
![demo](https://cdn.hackclub.com/01a0b68c-0eab-7822-83f3-ffde89f4b027/screen_recording_2026-09-18_150056.mp4)
