# FnxAmp

FnxAmp is an original, Winamp-inspired GEM audio player for the A2560Me and
FreeMiNT/XaAES. Start D:/APPS/FNXAMP/FNXAMP.APP. No EmuTOS ROM update is needed.
The VS1053b FPGA link requires build 70 or newer; the decoder is checked before
streaming. Audio goes through WM8776 analogue input AIN4 to the board's analogue
line/headphone outputs, not through HDMI. The existing firmware enables this
route; FnxAmp leaves the shared WM8776 mixer alone.

The playback worker is a separate FreeMiNT process with no AES calls. Audio
continues when another application is active, the window is iconified, a menu
is open or the GUI is waiting in a file chooser. Closing FnxAmp or shutting down
terminates its worker and cancels decoding cleanly. Only one FnxAmp instance
can own the decoder; an already-running hardware link is not taken over.

Open replaces the playlist and starts a file. Add queues a file. Change the
GEM selector's *.MP3 filter for WAV, OGG, WMA, AAC or MIDI files; those formats
are passed to the VS1053b unchanged. Load M3U accepts local relative/absolute
paths, up to 128 tracks. URLs are not fetched. Save writes a local M3U playlist.
Playlist rows select a song; Play or Enter starts it. Previous, Next, Repeat,
Shuffle and Remove manage playback. Shuffle plays each queued song before starting a new pass; Repeat controls
whether playback continues after the pass. Manual Next can start a new pass.

The progress bar shows bytes supplied to the decoder. Click it to seek in MP3
files only; seeking resynchronises to validated MPEG frames. MP3 duration and
seek positions are estimates for VBR files. The time counter is read from the
decoder, not an animation. ID3v1 and basic ID3v2.3/2.4 title/artist tags are
shown; unsupported text characters become question marks. WAV duration is
shown for conventional PCM headers. Other formats may show an unknown total.

Volume and balance use SCI_VOL, with 0.5 dB hardware attenuation steps. Bass
is a 0..15 dB boost below 60 Hz; treble is -8..7 steps of 1.5 dB above 10 kHz.
These are the decoder's real tone controls, not a simulated ten-band equalizer.
The bottom FIFO meter shows the FPGA's encoded-data queue, not audio amplitude
or a spectrum. Pause stops feeding data; a short decoder-buffer tail may play.

Keys: O Open, A Add, L Load M3U, Space/P Pause/resume, S Stop, Enter Play,
+/- Volume, Up/Down selection, Delete Remove. The fixed-size GEM window also
supports movement, normal application switching and iconification. It works in
monochrome, RGB565 and RGB888 without changing other applications' palettes.

Volume, balance, tone, repeat and shuffle settings are saved to
D:/APPS/FNXAMP/FNXAMP.INF. This file and the instance lock are target-local and
are excluded from releases and normal FTP updates. No listening history or
personal playlist is included in the release. The four owner-supplied test
songs are in D:/MUSIC; load TEST.M3U to play them. TRACKS.TXT retains the
supplied creator/title credits and TRACKS.JSON records SHA-256 checksums. DEMO.WAV is an original CC0 test
arpeggio and can be freely redistributed.

Hardware implementation: A2560Me Hardware Reference rev 1.8, VS1053b link,
pages 98-100; VLSI VS1053b datasheet v1.33, sections 10.5 and 10.11:
https://www.vlsi.fi/fileadmin/datasheets/vs1053.pdf

The worker writes big-endian four-byte FIFO words and handles trailing bytes
without padding the audio file. It uses FIFO room backpressure and bounded SCI
polling, not an interrupt hook. Natural EOF and cancellation follow the decoder
endFillByte/SM_CANCEL procedures; a stalled cancellation uses the FPGA's
software-reset startup sequence. IRQ vectors, global mixer settings, network
services and video modes are untouched.

Source is components/fnxamp in the FreeMiNT component; build
and drive integration is scripts/fnxamp.py in the system workspace. Source
copies are supplied separately under sources/drive-source/APPS/FNXAMP/SOURCE. Host regression:
python3 tests/fnxamp.py. On the board FNXTEST.TOS plays D:/MUSIC/DEMO.MP3
(or an audio filename argument) and writes D:/APPS/FNXAMP/FNXTEST.TXT.
The register/decoder test cannot establish that analogue speakers were heard.

The shared runtime image omits diagnostic executables and generated test tones. Audio static remains unresolved pending manufacturer confirmation.
