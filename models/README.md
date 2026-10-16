# ESP-SR models (`srmodels.bin`)

Flashed into the `model` partition (`partitions_sr.csv`) by `tools/pio_sr_model.py` on every USB upload.
Read on the board by `audio/WakeWord.cpp` (`esp_srmodel_init("model")`), which uses the word chosen by `set wakeword` (default "Hi ESP"). See wiki 14.

| Model | Wake word | Source |
|---|---|---|
| `wn9_jarvis_tts` | "Jarvis" (WakeNet9l, TTS-trained - missed real Thai voices, T064) | [esp-sr `model/wakenet_model/wn9_jarvis_tts`](https://github.com/espressif/esp-sr/tree/master/model/wakenet_model/wn9_jarvis_tts) |
| `wn9_hiesp` | "Hi ESP" (**default**, `set wakeword hiesp`) | same repo, `wn9_hiesp`; byte-identical to the pack that ships with esp-sr 2.5.3 in the framework |

`srmodels.bin` is not in git (`*.bin` is ignored). Without it the build flashes the framework's pack, which
also has "Hi ESP" (the default word) - only "Jarvis" needs the local pack.

Rebuild (model dirs = the folders from the esp-sr repo):

```
python tools/pack_sr_models.py pack models/srmodels.bin <dir>/wn9_jarvis_tts <dir>/wn9_hiesp
python tools/pack_sr_models.py list models/srmodels.bin
```

The models are Espressif's, under the esp-sr license (use on Espressif chips). Delete
`models/srmodels.bin` to fall back to the framework's pack ("Hi ESP" + VADNet + MultiNet, 3.2 MB).
