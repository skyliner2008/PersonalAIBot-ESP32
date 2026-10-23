# openWakeWord models (the phone's wake word "hey jarvis")

From the openWakeWord project by David Scripka - https://github.com/dscripka/openWakeWord (release v0.5.1):

| file | what |
|---|---|
| `melspectrogram.onnx` | audio (16 kHz) -> mel spectrogram |
| `embedding_model.onnx` | Google's speech embedding (TFHub `speech_embedding`, Apache 2.0) |
| `hey_jarvis_v0.1.onnx` | the "hey jarvis" classifier |

The pre-trained models are licensed **CC BY-NC-SA 4.0** (Creative Commons Attribution-NonCommercial-ShareAlike 4.0
International) by their author: free to use and share for non-commercial purposes, with this attribution, under the
same license. This app is a free, non-commercial project. The openWakeWord code is Apache 2.0; the app's Kotlin port
(`OpenWakeWord.kt`) follows openwakeword/utils.py and model.py (0.6).
