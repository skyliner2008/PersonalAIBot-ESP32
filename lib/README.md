# Third-party decoders

| Folder | What | License |
|---|---|---|
| `helix-mp3/` | RealNetworks Helix fixed-point MP3 decoder | RPSL 1.0 / RCSL 1.0 (`src/RPSL.txt`) |
| `helix-aac/` | RealNetworks Helix fixed-point AAC / HE-AAC decoder | RPSL 1.0 / RCSL 1.0 (`src/RPSL.txt`) |

Copied unmodified from the `libhelix-*` folders of
[earlephilhower/ESP8266Audio](https://github.com/earlephilhower/ESP8266Audio) 1.9.7. Only these RPSL-licensed
decoders are used - not the GPL-3.0 ESP8266Audio wrapper classes - so the rest of the project stays MIT.
Used by `src/audio/RadioPlayer.cpp` (Internet radio).
