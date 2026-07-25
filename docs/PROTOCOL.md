# Wireless Mic Protocol Specification v1

## 1. Overview

Three independent channels:

| Channel    | Transport | Purpose                                  |
|------------|-----------|-------------------------------------------|
| Discovery  | UDP mDNS + UDP Broadcast (fallback) | Find peers on LAN |
| Control    | TCP + TLS 1.3 (self-signed, pinned) | Pairing, commands, keepalive |
| Audio      | UDP (unencrypted payload, encrypted via DTLS-SRTP-lite) | Opus frames |

No internet, no external server. All traffic stays on the local subnet.

---

## 2. Discovery

### 2.1 mDNS (primary)

Service type: `_wiremic._udp.local.`

TXT records:
```
id=<uuid-v4>
name=<device display name>
model=<device model string>
platform=linux|android
proto=1
port=<control tcp port>
```

### 2.2 UDP Broadcast (fallback, networks without mDNS/multicast)

Port: `47500` (configurable)

Every 2000 ms, each instance broadcasts a `DISCOVER_ANNOUNCE` packet to
`255.255.255.255:47500` and listens on the same port.

Packet (JSON, UTF-8, max 512 bytes):

```json
{
  "type": "ANNOUNCE",
  "id": "d290f1ee-6c54-4b01-90e6-d701748f0851",
  "name": "Artin's Pixel 8",
  "model": "Pixel 8 Pro",
  "platform": "android",
  "protoVersion": 1,
  "controlPort": 47600,
  "ip": "192.168.1.34"
}
```

Devices are considered `OFFLINE` after `3` missed announce cycles (6000 ms)
and removed from the list after `15000 ms` of silence.

---

## 3. Pairing / Control Channel (TCP, port from ANNOUNCE, default 47600)

TLS 1.3 with a self-signed certificate generated on first run per device
(stored in app data dir). Trust-on-first-use (TOFU): the certificate
fingerprint (SHA-256) is shown to the user during the first pairing and
pinned locally afterward. Reconnects to a known device verify against the
pinned fingerprint and reject on mismatch.

### 3.1 Message Framing

All control messages: 4-byte big-endian length prefix + UTF-8 JSON body.

### 3.2 Message Types

**CONNECT_REQUEST** (initiator → target)
```json
{
  "type": "CONNECT_REQUEST",
  "requestId": "uuid",
  "device": {
    "id": "uuid",
    "name": "Artin's Pixel 8",
    "model": "Pixel 8 Pro",
    "platform": "android",
    "ip": "192.168.1.34",
    "connectionType": "wifi"
  },
  "certFingerprint": "sha256:ab12...",
  "audioCapabilities": {
    "sampleRates": [48000, 44100],
    "codec": "opus",
    "maxBitrateKbps": 128
  }
}
```

**CONNECT_RESPONSE** (target → initiator)
```json
{
  "type": "CONNECT_RESPONSE",
  "requestId": "uuid",
  "accepted": true,
  "reason": null,
  "audioSession": {
    "udpPort": 47700,
    "sampleRate": 48000,
    "channels": 1,
    "codec": "opus",
    "bitrateKbps": 96,
    "frameSizeMs": 10,
    "sessionKey": "base64-32-bytes"
  }
}
```
`accepted: false` includes a `reason` code: `REJECTED_BY_USER`,
`ALREADY_CONNECTED`, `UNSUPPORTED_CODEC`, `TIMEOUT`.

**PAIR_TRUST** (either direction, after first successful session, optional)
```json
{ "type": "PAIR_TRUST", "deviceId": "uuid", "trusted": true }
```
When both sides have `trusted: true` for each other, future
`CONNECT_REQUEST`s from that `deviceId` + matching cert fingerprint
auto-accept without a UI prompt (governed by the "Auto Connect" +
"Remember Trusted Devices" settings).

**DISCONNECT**
```json
{ "type": "DISCONNECT", "reason": "USER_REQUESTED" }
```

**KEEPALIVE / KEEPALIVE_ACK**
```json
{ "type": "KEEPALIVE", "seq": 42 }
{ "type": "KEEPALIVE_ACK", "seq": 42 }
```
Sent every 3000 ms. 3 consecutive missed ACKs → connection considered
lost → auto-reconnect attempted for up to 30 s before surfacing
`Offline` in the UI.

**ERROR**
```json
{ "type": "ERROR", "code": "INVALID_STATE", "message": "..." }
```

### 3.3 Connection State Machine

```
IDLE -> DISCOVERING -> REQUEST_SENT -> AWAITING_APPROVAL
AWAITING_APPROVAL -> ACCEPTED -> STREAMING
AWAITING_APPROVAL -> REJECTED -> IDLE
STREAMING -> DISCONNECTED -> RECONNECTING -> STREAMING | IDLE
```

Request timeout: 20000 ms. If no `CONNECT_RESPONSE` arrives, the
initiator surfaces `TIMEOUT` and returns to `IDLE`.

---

## 4. Audio Channel (UDP)

Negotiated `udpPort`/`sessionKey` come from `CONNECT_RESPONSE`. Every
packet is authenticated and encrypted with ChaCha20-Poly1305 using
`sessionKey`, nonce = 8-byte sequence + 4-byte random salt exchanged once
at session start.

### 4.1 Codec

**Opus**, `OPUS_APPLICATION_RESTRICTED_LOWDELAY`, mono, 48 kHz preferred
(falls back to whatever both sides negotiate in `audioCapabilities`),
10 ms frames (configurable 5/10/20 ms via "Latency Mode" setting),
variable bitrate 32–128 kbps depending on "Audio Quality" setting.
Raw PCM is never sent except as an explicit last-resort fallback if Opus
initialization fails on either endpoint, and only after logging a
warning — this is not the default path.

### 4.2 Packet Format

```
byte 0        : version (1)
byte 1        : flags (bit0=marker/keyframe-equivalent, bit1=DTX silence)
bytes 2-9     : sequence number (u64 BE)
bytes 10-13   : capture timestamp, monotonic ms (u32 BE)
bytes 14-17   : nonce salt (u32 BE)
bytes 18-33   : Poly1305 tag (16 bytes)
bytes 34..N   : ChaCha20 ciphertext (Opus payload)
```

Max UDP payload kept under 1200 bytes to avoid IP fragmentation.

### 4.3 Jitter Buffer & Loss Recovery

Adaptive jitter buffer on the Linux receiver:
- target depth starts at 2 frames (20 ms @10ms frames)
- grows by 1 frame when interarrival jitter variance exceeds threshold,
  shrinks by 1 frame after 500 ms of stability, bounded [1, 8] frames
- missing sequence numbers trigger Opus PLC (packet loss concealment)
  via `opus_decode` with a null payload for that frame
- out-of-order packets within the buffer window are reordered by
  sequence number; packets older than the current playout point are
  dropped

DTX: the Android sender may mark silence frames (`flags bit1`) and send
them at a reduced rate (1 per 400 ms) instead of every 10 ms; the
receiver sustains comfort silence between them.

---

## 5. Sample Rate / Format Negotiation

1. Both sides report supported rates in `audioCapabilities.sampleRates`.
2. The responder picks the highest mutually supported rate and echoes it
   in `audioSession.sampleRate`.
3. If Android's `AAudio` stream cannot open at the negotiated rate, it
   renegotiates once via a `RENEGOTIATE` control message before falling
   back to its nearest supported rate.

---

## 6. Security Summary

- TLS 1.3 control channel, TOFU cert pinning per paired device.
- Per-session symmetric key (`sessionKey`) delivered only after user
  approval, over the already-TLS-encrypted control channel.
- Audio payload additionally encrypted (ChaCha20-Poly1305) since it
  travels over plain UDP.
- Unknown/unpaired devices always require an explicit Accept tap/click;
  there is no silent first-time pairing.
- Trusted-device auto-accept only applies to a `deviceId` whose current
  connection also presents the previously pinned cert fingerprint.

---

## 7. Versioning

`protoVersion` is included in every discovery announce and
`CONNECT_REQUEST`. A peer with a higher major version than it can
handle responds with `ERROR / UNSUPPORTED_PROTOCOL` instead of
attempting to interoperate.
