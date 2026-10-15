---
tags: [gemini, websocket, protocol, api, audio, function-calling]
created: 2026-10-14
updated: 2026-10-14
status: implemented
---

# 🌐 Gemini Live API Protocol (as implemented in v2)

> **Related:** [[00-Home-Dashboard]] | [[04-System-Architecture]] | [[08-Long-Term-Memory-GraphRAG]] | [[07-Troubleshooting-Log]]
> **Code:** `src/ai/GeminiLiveClient.*` (transport + parsing) · `src/agent/AgentController.cpp` (setup + events) · `src/agent/AgentTools.cpp` (tools)
> ตรวจสอบกับเอกสาร Gemini API ล่าสุด (ai.google.dev) เมื่อ 2026-10-14

---

## Key Specifications

| รายการ | ค่า |
|---|---|
| Model | เลือกในเมนู: `gemini-3.8-live` (ค่าเริ่มต้น) / `gemini-3.8-live-extended-thinking` / `gemini-3.1-flash-live-preview` — **สลับอัตโนมัติเมื่อใช้ไม่ได้** ([[09-Assistant-Profiles-and-Tools#5. Auto fallback ของโมเดล]]) |
| Endpoint | `wss://generativelanguage.googleapis.com/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent?key=API_KEY` |
| Transport | ESP-IDF `esp_transport_ws` (handshake + ส่ง) + SSL บน task `geminiIO` ตัวเดียว (v3.1 — เดิม `esp_websocket_client` มี race จนแครช: T039) · **อ่าน frame เอง** ตรงจาก TLS แบบวนจนครบทุกไบต์ (v3.3 — ตัวอ่านของ IDF 4.4 อ่าน header ขาดเมื่อ TLS record ตัดกลาง แล้วเหลื่อมไปอ่านเสียงเป็น close: T044) |
| TLS | ตรวจใบรับรองกับ GTS Root R1/R3/R4 + GlobalSign ECC R4 (`src/ai/GoogleRootCA.h`) |
| Input audio | PCM 16-bit mono **16 kHz** LE, Base64, chunk 40 ms (`audio/pcm;rate=16000`) |
| Output audio | PCM 16-bit mono **24 kHz** LE, Base64 ใน `inlineData` |
| Response modality | `AUDIO` เท่านั้น (ข้อความได้จาก `outputAudioTranscription`) |
| Session | context window compression `triggerTokens 12000 → targetTokens 5000` (งบ 65k TPM); session resumption เปิด |
| Input gating | Local VAD: ส่งเฉพาะช่วงที่พูด + `audioStreamEnd`; ปิด session เมื่อพัก |

> [!NOTE] ข้อแตกต่างของ gemini-3.8-live ที่กระทบโค้ด
> - Function calling เป็น **NON_BLOCKING โดย default** → เราประกาศ `behavior: "BLOCKING"` ทุก tool
> - ห้ามส่ง `thinkingLevel` ใน setup · proactive audio เปิดถาวร (โมเดลอาจไม่ตอบเสียงที่ไม่เกี่ยว)
> - ข้อความเดียวอาจมีทั้ง audio + transcript + `turnComplete` → ต้องประมวลผลทุก field ไม่ return ก่อน

---

## ทำไมเปลี่ยนจาก links2004/WebSockets

| ปัญหาที่พบ (v1) | ผล | v2 |
|---|---|---|
| `WEBSOCKETS_MAX_DATA_SIZE` = 15 KB และ **disconnect 1009** เมื่อ frame ใหญ่กว่า | ข้อความเสียงยาวจาก Gemini ทำให้หลุดการเชื่อมต่อ | ประกอบ frame/chunk ขนาดใดก็ได้ (สูงสุด 1 MB) ใน PSRAM |
| `beginSSL()` ไม่ใส่ CA → ไลบรารีเรียก `setInsecure()` | ถูกดัก API key ได้ (MITM) | `cert_pem` = Google roots |
| `WStype_BIN` แค่พิมพ์ log | ถ้า server ส่ง JSON เป็น binary frame จะไม่เคยเห็น `setupComplete` | text และ binary ใช้ parser เดียวกัน |

การประกอบข้อความ (task `geminiIO`, v3.3): `readOneFrame()` อ่าน header/ความยาว/payload ครบด้วย `readExact()` → ต่อเข้า buffer ใน PSRAM → เมื่อ frame มี **FIN** (และ `jsonframe::isCompleteObject()` ยืนยัน) → copy เข้า queue → agent task เรียก `poll()` แล้ว parse ด้วย ArduinoJson (allocator ใน PSRAM)

---

## ลำดับข้อความ

```mermaid
sequenceDiagram
    autonumber
    participant ESP as 📟 ESP32 (agent task)
    participant GEM as ☁️ gemini-3.8-live
    ESP->>GEM: WSS connect (TLS verified)
    ESP->>GEM: {"setup": {... systemInstruction(core memory) ... tools ... sessionResumption}}
    GEM-->>ESP: {"setupComplete": {}}
    loop ผู้ใช้พูด (mic gate เปิด)
        ESP->>GEM: {"realtimeInput":{"audio":{"mimeType":"audio/pcm;rate=16000","data":"..."}}}
    end
    GEM-->>ESP: {"serverContent":{"inputTranscription":{"text":"..."}}}
    GEM-->>ESP: {"toolCall":{"functionCalls":[{"id":"..","name":"recall_memory","args":{...}}]}}
    ESP->>GEM: {"toolResponse":{"functionResponses":[{"id":"..","name":"recall_memory","response":{"result":{...}}}]}}
    loop AI ตอบ (mic gate ปิด)
        GEM-->>ESP: {"serverContent":{"modelTurn":{"parts":[{"inlineData":{"data":"<pcm24k>"}}]},"outputTranscription":{"text":"..."}}}
    end
    ESP->>GEM: {"realtimeInput":{"audioStreamEnd":true}}  (ตอนปิด gate)
    GEM-->>ESP: {"serverContent":{"generationComplete":true}} / {"turnComplete":true}
    GEM-->>ESP: {"sessionResumptionUpdate":{"newHandle":"...","resumable":true}}
    GEM-->>ESP: {"goAway":{"timeLeft":"10s"}} → restart() พร้อม handle
```

---

## Setup Message (สร้างใน `AgentController::buildSetup`)

```json
{
  "setup": {
    "model": "models/gemini-3.8-live",
    "generationConfig": {
      "responseModalities": ["AUDIO"],
      "speechConfig": { "voiceConfig": { "prebuiltVoiceConfig": { "voiceName": "Aoede" } } }
    },
    "systemInstruction": { "parts": [{ "text": "คุณคือ \"<ชื่อผู้ช่วย>\" ... ผู้ใช้ชื่อ ... นิสัย: ... การพูด: ... ลงท้าย ค่ะ/ครับ ... ดวงตา ... ความจำ ... ## ตอนนี้ ... ## สิ่งสำคัญที่คุณจำได้ ... ## บทสนทนาล่าสุด ..." }] },
    "tools": [{ "googleSearch": {} }, { "functionDeclarations": [ /* 20 tools, ดู [[09-Assistant-Profiles-and-Tools]] */ ] }],
    "inputAudioTranscription": {},
    "outputAudioTranscription": {},
    "contextWindowCompression": { "triggerTokens": 12000, "slidingWindow": { "targetTokens": 5000 } },
    "sessionResumption": { "handle": "<ถ้ามี>" }
  }
}
```
- `voiceName` = เสียงจากการตั้งค่า (30 เสียง) · `generationConfig.thinkingConfig.thinkingLevel` ใส่เฉพาะ Extended Thinking
- ขนาดจริง ~10-14 KB (WebSocket buffer 32 KB = frame เดียว) · persona/ชื่อ/เสียง/โมเดล มาจาก NVS settings

### ตัวอย่าง function declaration
```json
{
  "name": "recall_memory",
  "description": "Search long-term memory (facts and knowledge graph) stored on this device. Call it BEFORE answering whenever ...",
  "behavior": "BLOCKING",
  "parameters": {
    "type": "OBJECT",
    "properties": {
      "query": { "type": "STRING", "description": "Short keywords ... retry with synonyms" },
      "limit": { "type": "INTEGER", "description": "Max results, 1-10. Default 5." }
    },
    "required": ["query"]
  }
}
```

### Tool response
ทุก call ได้คำตอบเสมอ (รวม tool ที่ไม่รู้จัก → `{"error":"unknown function"}`) ไม่งั้นโมเดลจะรอค้าง
```json
{"toolResponse":{"functionResponses":[{"id":"call-1","name":"save_memory","response":{"result":{"status":"saved","memory_id":7,"total_memories":42}}}]}}
```

---

### แจ้งเตือนจากอุปกรณ์ (clientContent)
เมื่อ timer/reminder ครบกำหนด อุปกรณ์ส่งข้อความให้โมเดลพูดบอกผู้ใช้:
```json
{"clientContent":{"turns":[{"role":"user","parts":[{"text":"[แจ้งเตือนจากระบบ] ถึงเวลา: กินยา — บอกผู้ใช้สั้นๆ"}]}],"turnComplete":true}}
```

## Server Messages ที่จัดการ

| Field | การจัดการ |
|---|---|
| `setupComplete` | state = Ready, เปิด mic gate |
| `serverContent.modelTurn.parts[].inlineData.data` | Base64 → PCM → playback ring (ถ้าไม่ได้กดหยุดเอง) |
| `serverContent.inputTranscription.text` | สะสมเป็น turn ผู้ใช้ + ซับไตเติลสีฟ้า |
| `serverContent.outputTranscription.text` | สะสมเป็น turn AI + ซับไตเติลสีขาว |
| `serverContent.interrupted` | ล้างเสียงที่ค้าง, บันทึก turn AI ว่าถูกขัด |
| `serverContent.generationComplete` / `turnComplete` | เล่นเสียงที่เหลือให้จบ, บันทึก turn ลง episodic log |
| `toolCall.functionCalls[]` | `AgentTools::execute` → `toolResponse` |
| `toolCallCancellation.ids` | log |
| `sessionResumptionUpdate` | เก็บ `newHandle` ถ้า `resumable` |
| `goAway.timeLeft` | reconnect หลังเล่นเสียงจบ (resume ด้วย handle) |
| `usageMetadata.totalTokenCount` | สะสมเป็นโทเคน/นาที (หน้าข้อมูลระบบ) |
| close frame (code + reason) | จัดประเภท (quota/unavailable/not found/auth) → **auto fallback** หรือ ERROR ถ้า API key ผิด |

**Resume ล้มเหลว 2 ครั้งติด** → ทิ้ง handle แล้วเริ่ม session ใหม่ (ความจำระยะยาวยังอยู่)
**กดค้าง 1.5 s** → เริ่ม session ใหม่โดยตั้งใจ

### บทเรียนจากบอร์ดจริง (v3.1, 2026-10-14)

- **โควตา Google Search หมด = ทั้ง session ถูกปิด** ด้วย `1011 "You exceeded your current quota, please check your plan and billing details..."`
  เหมือนโควตาโมเดลหมดทุกประการ — ยืนยันด้วย `setupmode nosearch` (ต่อได้ทันที) → firmware ลองใหม่โดยไม่มี `googleSearch` เอง
  ([[07-Troubleshooting-Log#T028 "You exceeded your current quota" ทั้งที่ยังไม่ถึงลิมิต - โควตา Google Search]])
- close reason ถูกจำกัด 123 ไบต์ (ข้อกำหนด WebSocket) จึงไม่เห็น `QuotaFailure` → ใช้ `apitest` (REST + header `x-goog-api-key`) ดูรายละเอียด
- `?key=` (ตามเอกสาร) กับ header `x-goog-api-key` (แบบ SDK) ให้ผลเหมือนกันกับ auth key รูปแบบ `AQ.` — เลือกได้ด้วย `keymode` เพื่อวินิจฉัย
- ถ้าเซิร์ฟเวอร์รับ socket แต่ไม่ส่ง `setupComplete` ภายใน **15 วิ** → reconnect (นับเป็น transient)
- **ปิด session ด้วย close handshake เสมอ** (ส่ง close frame 1000 แล้วรอ close ตอบ ≤1.5 วิ) — ตัดแค่ TCP ทำให้ session ค้างฝั่งเซิร์ฟเวอร์และกินโควตา (T035)
- **task เดียวถือการเชื่อมต่อ** (`geminiIO`): task อื่นแค่เข้าคิวข้อความ — ส่งเสียงไม่ทันบน Wi-Fi อ่อนจึงไม่ทำให้ TLS session ถูกปิดจากสอง task พร้อมกัน (T039)
- `1008 "BidiGenerateContent session not found or expired"` = resumption handle หมดอายุ → เริ่ม session ใหม่ ไม่ใช่โมเดลหาย (T034)
- setup ขั้นต่ำสำหรับทดสอบ: `{"setup":{"model":"models/gemini-3.8-live","generationConfig":{"responseModalities":["AUDIO"]}}}` (96 ไบต์) — `setupmode min`

---

## Data Format Quick Reference

| | Input (mic) | Output (speaker) |
|---|---|---|
| Rate | 16 kHz (resample จาก I2S 24 kHz) | 24 kHz |
| Chunk | 40 ms = 640 samples = 1,280 B → Base64 1,708 B | ตามที่ server ส่ง (อาจหลายสิบ KB) |
| Bandwidth | ~43 KB/s ขาขึ้น (ระหว่างผู้ใช้พูด) | ~64 KB/s Base64 ขาลง |

---

## ความปลอดภัยของ API key

- key อยู่ใน NVS (ค่าเริ่มต้นจาก `include/secrets.h` ซึ่งไม่ commit) และถูกส่งใน query string **ผ่าน TLS ที่ตรวจใบรับรองแล้ว**
- serial console / PC app ไม่เคยแสดง key — `config` มีแค่ `keyLen` + `keyHint` (3 ตัวแรก…4 ตัวท้าย)
- ใครเข้าถึงตัวบอร์ดสามารถอ่าน flash ได้ → ใช้ key ที่จำกัดสิทธิ์/โควตาสำหรับอุปกรณ์นี้
- ทางเลือกในอนาคต: ephemeral token จาก backend ของตัวเอง (Live API รองรับ แต่ต้องมี server กลาง)
