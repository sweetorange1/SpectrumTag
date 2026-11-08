# SpectrumTag 1.4.23 — standalone spectrum video

This version adds a separate animated spectrum export to the standalone. It does not replace the existing source-format audio/video export. No Stable designation, Plan B refresh, GitHub publication or macOS/AU build is implied.

## User contract

- Pure spectrum animation with the actually processed soundtrack. A Tag is revealed by the audio spectrum as playback reaches it; no opaque source-image overlay substitutes for the rendered spectrum.
- Follow each Tag's saved physical time/frequency placement and image aspect. Smoothly blend horizontal zoom and frequency mappings before its start. Keep audio at normal speed; never rewrite editor Speed or edit parameters for the video.
- Earlier-starting overlapping Tags own the camera until their end. A fully nested later Tag does not steal it; a longer later Tag may be followed for its remaining duration. One frequency view cannot simultaneously match incompatible overlapping scales.
- Select MP4/MOV/MKV (H.264/AAC) or WebM (VP9/Opus), 720p/1080p/1440p/2160p, 30/60 fps. Default MP4, 1080p, 30 fps; remember export settings separately from editor preferences.
- With no source file, the standalone offers a blank timeline. Print extent supplies the duration. Noise Fill above zero supplies actual sound in blank areas; zero leaves silence. Audio defaults to 24-bit stereo 48 kHz WAV when no source format exists.
- The existing editable extent limit remains 15 minutes / 64 million samples. This first video layout is landscape 16:9.

## Implementation

Video reads an immutable RecordingSnapshot and the existing processed FFT tiles. Camera target size is derived from actual Tag duration, physical band and image aspect, scaled to the selected output dimensions. A quintic interpolation blends horizontal log scale and monotonic forward frequency mappings. Inversion is computed per row, never per pixel. The future side of the playhead stays blank.

Frames are sent through a cancellable byte pipe into the bundled encoder. No raw frame sequence is saved. Only a temporary processed WAV and encoded output are needed. A unique output is atomically committed after encoder success; cancelling or failing preserves an existing destination. The message thread remains responsive, editing is held during export, and closing requests cancellation.

Windows uses overlapped named-pipe I/O; POSIX uses a non-blocking FIFO and thread-local SIGPIPE masking. The macOS path is retained for later compilation, not claimed as tested by Windows verification. APIs were checked against Microsoft Learn's named-pipe overlapped I/O documentation and FFmpeg's rawvideo documentation.

The source-free canvas retains one silent anchor sample and derives the remainder from Prints. It reuses existing immutable-source rendering, deletion and undo behavior rather than baking noise into source audio. Generated-source identity and session format must survive session restoration.

## Verification

See the current delivery report under D:\Codex\Temp\SpectrumTag-1.4.23 for completed builds and tests. Do not infer success merely from this implementation note.
