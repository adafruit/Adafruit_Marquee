# Adafruit_MQTT: notes from making it carry a 21 KB payload

Working notes from wiring the Marquee bitmap feed (a whole base64-encoded BMP in
one feed value) through `Adafruit_MQTT` on an ESP32-S2. Recorded because most of
what follows is not obvious from the library's API, and two of the traps cost a
full debug cycle each.

Line references are to the fork at `~/Documents/Arduino/libraries/Adafruit_MQTT_Library`
(branch `marquee-large-payload`, forked from registry 2.6.5 / 2.6.6 — sources are
byte-identical between those two, only `library.properties` differs).

---

## 1. The constraint

`Adafruit_MQTT` sizes every buffer at compile time, and the sizes are inline
arrays inside objects:

| Array | Where | Default | Multiplicity |
|---|---|---|---|
| `Adafruit_MQTT::buffer` | `Adafruit_MQTT.h` | `MAXBUFFERSIZE` = 512 on ESP32 | one per client |
| `Adafruit_MQTT_Subscribe::lastread` | `Adafruit_MQTT.h` | `SUBSCRIPTIONDATALEN` = `MAXBUFFERSIZE` | **one per subscription** |

`buffer` is a single buffer used for *all* incoming and outgoing packets. The
header says the quiet part out loud:

> Future TODO: This should be replaced by the ability to dynamically allocate a
> buffer as needed.

The payload we needed to receive is ~20,980 base64 chars (a 15,734-byte BMP).
Three limits stack up before it can reach a callback:

1. `readFullPacket()` reads at most `MAXBUFFERSIZE` bytes of the packet.
2. `handleSubscriptionPacket()` then truncates the payload into `lastread`.
3. If you go through `AdafruitIO_Feed`, `AdafruitIO_Data::setCSV()` `strcpy`s
   into `_csv[262]` (`AIO_CSV_LENGTH`) and `setValue()` into `_value[45]`
   (`AIO_DATA_LENGTH`) — **both unbounded**. A >261-byte datum corrupts memory
   before your callback runs. This is why the bitmap feed does not use
   `AdafruitIO_Feed` at all.

### Why naively raising the constant does not work

`SUBSCRIPTIONDATALEN` defaulted to `MAXBUFFERSIZE`, and `lastread` is per
subscription. This project has four subscriptions (AdafruitIO's `_err_sub` and
`_throttle_sub`, plus sleep and bitmap). Raising both constants to 22528:

```
buffer            22,528 x 1 =  22,528
lastread          22,528 x 4 =  90,112
AdafruitIO _value 22,528 x 2 =  45,056
AdafruitIO _csv   22,528 x 2 =  45,056
                               -------
                               202,752  (~198 KiB)
```

That does not fit in the S2's 320 KiB of internal SRAM alongside the mbedTLS
handshake (~85 KiB measured), so the constant alone is not the answer.

### Why the same payload works fine in CircuitPython

Not a protocol difference — allocation strategy. `adafruit_minimqtt` reads the
MQTT Remaining Length varint (`adafruit_minimqtt.py:1063`, bounded only by
`MQTT_MSG_MAX_SZ = 268435455` at `:60`), then calls
`_sock_exact_recv(sz)` (`:1083`), which allocates `bytearray(bufsize)` at the
payload's *actual* size (`:1126`) and loops `recv_into` until it has all of it.
The only ceiling is free heap. MiniMQTT is that TODO, implemented.

---

## 2. What we changed

### Heap-allocate both buffers, prefer PSRAM

`mqtt_alloc_buffer()` (`Adafruit_MQTT.cpp:120`) tries `ps_malloc` when
`BOARD_HAS_PSRAM` is defined, falls back to `malloc`, and zeroes on success.
`Adafruit_MQTT::buffer` and `Adafruit_MQTT_Subscribe::lastread` both became
pointers allocated through it.

Result on hardware: 22 KB packet buffer + 22 KB bitmap payload buffer both land
in PSRAM, internal SRAM cost ~1.5 KB, static RAM unchanged at 17.5%.

### Size each subscription independently

`Adafruit_MQTT_Subscribe` gained a `datalen_max` constructor argument
(`Adafruit_MQTT.cpp:1047`) defaulting to `SUBSCRIPTIONDATALEN`, plus a
destructor. Only the bitmap subscription asks for 22528; the other three stay at
the 512 default. `SUBSCRIPTIONDATALEN` is now deliberately **decoupled** from
`MAXBUFFERSIZE`.

Both defines are `#ifndef`-guarded so a build can override them
(`-DMAXBUFFERSIZE=22528`). Note: with the buffers on the heap, the
per-subscription argument is arguably unnecessary on a PSRAM board — 4 x 22 KB
of 2 MB PSRAM is 4.5% and zero internal SRAM. It earns its keep only on parts
without PSRAM, where 90 KB of 320 KiB would be fatal.

### Drain oversized packets

Upstream `readFullPacket()` reads what fits, logs `"Packet too big for buffer"`
(`:368`) and returns — leaving the rest of the PUBLISH in the socket. The next
call then parses payload bytes as an MQTT fixed header, and the connection
desyncs permanently. The fork now drains the remainder through a 64-byte sink so
an oversized message is dropped instead of poisoning the stream. This is a
correctness fix independent of the size change.

---

## 3. Traps, in order of how much time they cost

### `sizeof(buffer)` — the expensive one

Changing `uint8_t buffer[MAXBUFFERSIZE]` to `uint8_t *buffer` silently changes
`sizeof(buffer)` from 512 to 4. Two callers meant "the allocation size":

- `Adafruit_MQTT::publish()` passed it as `publishPacket()`'s `maxPacketLen`
  (now `bufferSize`, `:447`).
- `Adafruit_MQTT_Client::connectServer()` used it to `memset` before `strcpy`ing
  the server name (now `bufferSize`, `Adafruit_MQTT_Client.cpp:30`).

The first one crashes on the **first publish**, and the arithmetic is worth
understanding because it is a booby trap in upstream code. `publishPacket`
(`:858`) has a "gracefully shrink the payload rather than corrupt memory" branch:

```c
bLen = maxPacketLen - (len + 2 + packetAdditionalLen(maxPacketLen));   // :885
```

With `maxPacketLen = 4`, a 43-char topic and `bLen = 0`: `len` = 45, the fit
check fails, and this computes `4 - 47` on a `uint16_t` = **65493**. Then it
`memmove`s 65493 bytes from a 1-byte source. That branch has no lower bound, so
it turns any undersized `maxPacketLen` into a ~64 KB out-of-bounds read/write.

**Lesson:** after an array-to-pointer change, grepping for the size *macro* is
not enough. `sizeof(x)` is an aliased reference to the size that never names the
macro. Audit every use of the identifier.

### `uint16_t` underflow, generally

Same shape bit us twice. `lastread_max - 1` promotes to `int`, so with
`lastread_max == 0` (failed allocation) it yields -1, which assigned back to a
`uint16_t` length becomes 65535. Any `capacity - overhead` expression on an
unsigned type needs the capacity checked first.

### `publish()` overload resolution

```cpp
_pub->publish("\0", 1);   // NOT (payload, length)
```

`Adafruit_MQTT_Publish` has `publish(const char *, bool retain)` and
`publish(uint8_t *, uint16_t bLen, bool retain)`. A `const char *` plus an int
binds to the **first** one, making `1` the `retain` flag — it silently publishes
a *retained* empty message. `AdafruitIO_Feed::get()` gets this right with a bare
`publish("\0")`.

### `subscribe()` does not subscribe

`Adafruit_MQTT::subscribe()` only records the pointer in the `subscriptions[]`
array. The SUBSCRIBE packets are sent by `Adafruit_MQTT::connect()`, which walks
every non-null entry and waits for each SUBACK. So subscriptions must be
registered *before* `connect()`, and any subscription added afterwards is
silently inert until the next reconnect.

### `lastread` is a public member

Making it a pointer is technically a breaking API change for any sketch that
touches `sub->lastread` directly or takes its `sizeof`. Acceptable in a fork;
worth calling out if upstreaming.

### Reaching the MQTT client from outside AdafruitIO

`AdafruitIO::_mqtt` is `protected` and only befriends its own feed/group/time
classes; `AdafruitIO_Feed::_sub` is `private`. A thin subclass exposes it. But
`AdafruitIO_WiFi` is a **typedef** (to `AdafruitIO_ESP32` on ESP32), not a class,
so `using AdafruitIO_WiFi::AdafruitIO_WiFi;` will not compile — inheriting-
constructor syntax needs the base's injected-class-name. Spell the constructor
out; a typedef-name is legal in the mem-initializer.

---

## 4. Guardrails now in the fork

Allocation failure is reported, not assumed away. maxBufferSize sits at zero when buffer isnt allocated
Checks on the paths that consume it:

| Function | Guard | Behaviour |
|---|---|---|
| `Adafruit_MQTT::connect()` | `buffer == NULL \|\| bufferSize == 0` | returns -1 |
| `Adafruit_MQTT_Client::connectServer()` | same, plus `strlen(servername) >= bufferSize` | returns false |
| `Adafruit_MQTT::readFullPacket()` | `buffer == NULL \|\| maxsize < 8` (`:326`) | returns 0 |
| `handleSubscriptionPacket()` | `lastread == NULL \|\| lastread_max < 2` (`:704`) | drops the message |
| `Adafruit_MQTT_Subscribe` ctor | clamps `datalen_max` to >= 2 | 1 byte payload + NUL |

`readFullPacket`'s `maxsize < 8` exists specifically to stop the
`maxsize - (pbuff - buffer) - 1` expression from underflowing.

### Sizing relationship

`SUBSCRIPTIONDATALEN` (or a subscription's `datalen_max`) bounds the PUBLISH
**payload** only. The packet buffer must also hold the framing and the topic:

```
MAXBUFFERSIZE >= payload + 9 + strlen(topic)
```

The 9 is PUBLISH framing, worst case: 1 fixed header + up to 4 remaining-length
+ 2 topic-length + 2 packet-id when QoS > 0. (There is no named constant for
this in the fork today; it is written out here so the relationship is at least
recorded somewhere.) One further byte of the payload
buffer is reserved for the NUL terminator, so usable payload is
`lastread_max - 1`.

**Current config is slightly wrong in principle:** `MQ_BITMAP_SUB_LEN` and
`MAXBUFFERSIZE` are both 22528, so a maximum-size payload could not fit through
the packet buffer. The real ceiling is about 22478 with a 44-char topic. It does
not bite at 20,980 bytes, but `MAXBUFFERSIZE = MQ_BITMAP_SUB_LEN + 256` would be
honest.

---

## 5. Not done

- **`ping()` (`:749`) and `disconnect()` (`:423`) are unguarded.** Both write
  through `buffer` via `pingPacket()` / `disconnectPacket()`, which take no size
  argument. `ping()` matters more because AdafruitIO calls it on the keepalive
  path, not just from `connect()`. Note `disconnect()` needs the guard around
  the packet send only — an early return would skip `disconnectServer()` and
  leak the socket.
- **Fits-check.** Proposed shape: `#error` at compile time if
  `SUBSCRIPTIONDATALEN > MAXBUFFERSIZE` (unarguable), plus a runtime
  `ERROR_PRINTLN` in the `Subscribe` constructor doing the exact arithmetic with
  the real topic length. `MQTT_ERROR` is on by default so that surfaces without
  extra flags. Avoid a compile-time warning based on a *guessed* topic
  allowance: upstream ships `SUBSCRIPTIONDATALEN == MAXBUFFERSIZE`, so any
  headroom check fires on a stock build and teaches people to ignore it.
- **Upstreaming.** The heap-allocation change plus the drain fix are both
  defensible upstream. The `lastread` visibility change is the sticking point.
- The fork is referenced from `platformio.ini` as
  `symlink://../Adafruit_MQTT_Library`, which works locally but needs a pinned
  git URL before CI.

---

## 6. Debugging note: ESP32-S2 cannot show you the panic

Relevant because it shaped how the `sizeof(buffer)` bug was found. On the S2 a
panic backtrace never reaches the host:

- `SOC_USB_SERIAL_JTAG_SUPPORTED` is absent on S2 (it is `1` on S3) — USB is OTG
  only, driven in software by TinyUSB.
- `CONFIG_ESP_CONSOLE_UART_DEFAULT=y`, `CONFIG_ESP_CONSOLE_UART_NUM=0` — the IDF
  console, and therefore the panic handler, writes to UART0 (GPIO43/44).
- `CONFIG_ESP_SYSTEM_PANIC_PRINT_REBOOT=y` with a 0-second delay.

The panic handler runs with interrupts disabled and cannot drive a software USB
stack, so the backtrace goes out GPIO43 and the USB CDC device disappears with
the reset. `esp32_exception_decoder` is not misconfigured — nothing arrives.
`CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y` is set but unusable: the
`tinyuf2-partitions-4MB.csv` table has no `coredump` partition and the 4 MB is
fully allocated.

Two workarounds:

1. A USB-serial adapter on the board's TX pin (GPIO43) at 115200, monitored
   separately. The decoder works normally on that stream.
2. Flushed step markers plus `esp_reset_reason()` at boot. The reset reason
   distinguishes brownout / panic / watchdog, and the last marker printed names
   the statement. This localised a crash with no obtainable backtrace in three
   flashes. `uxTaskGetStackHighWaterMark(NULL)` in each marker rules stack
   exhaustion in or out — here it never dropped below 4,492 bytes, which is what
   ruled out the `ARDUINO_LOOP_STACK_SIZE` theory.

Measured numbers from a healthy bring-up, for future comparison:

```
internal heap free: 146,880 at setup -> 143,660 after buffers -> 58,948 after TLS
PSRAM free:       2,081,140 at setup -> 2,015,972 after TLS
loopTask stack remaining: 6,432 -> 4,492 (of 8,192)
```
