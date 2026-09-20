# Third-party components

This repository copies none of these. The build fetches them as flake inputs (or takes `-DPBE_*_DIR` paths) and compiles them into the plugin binary. The binary therefore contains code and data under the licenses below.

| component | author | used for | license |
|---|---|---|---|
| [CPLUG](https://github.com/Tremus/CPLUG) | Tré Dudman | VST3 and CLAP format wrappers | MIT or public domain, at the user's choice |
| [CLAP](https://github.com/free-audio/clap) (bundled in CPLUG) | Alexandre Bique | CLAP API header | MIT |
| VST3 C API, `vst3_c_api.h` (bundled in CPLUG) | Steinberg Media Technologies | VST3 interface definitions | [VST 3 SDK license](https://www.steinberg.net/sdklicenses): GPLv3 or Steinberg's proprietary license agreement |
| [pugl](https://github.com/lv2/pugl) | David Robillard | window embedding, GL context, input | ISC |
| [NanoVG](https://github.com/memononen/nanovg), with stb_truetype | Mikko Mononen, Sean Barrett | editor drawing and text | zlib; stb: MIT or public domain |
| [DejaVu Sans](https://dejavu-fonts.github.io/) | DejaVu fonts project | editor font, embedded at build time | Bitstream Vera license; permits redistribution and embedding |
| [Unity](https://github.com/ThrowTheSwitch/Unity) | ThrowTheSwitch | unit tests only; not linked into shipped binaries | MIT |

To distribute a VST3 plugin binary, comply with Steinberg's VST 3 SDK license: release it under GPLv3 or sign Steinberg's proprietary license agreement. The CLAP build has no such requirement. This repository contains no Steinberg code; the build takes the header from CPLUG.
