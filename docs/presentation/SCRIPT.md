# Group 15 presentation: speaker scripts

The talk is 10 minutes. Mujuni opens with the system overview, then each member presents
what their part does and the code they wrote, in this order. These are the same notes
the presenter window shows (press P in the deck).

## Files

- `index.html`: the deck. Open it in Chrome or Edge.
- `CoCIS-Group15-slides.pdf`: one slide per page. A backup if the HTML will not open.
- `CoCIS-Group15-rehearsal.pdf`: every slide with its script underneath, for rehearsing.

## Running the deck

- **Full screen:** press **F**, or move the mouse and click **Full screen** in the top-right
  corner (F11 also works). **Esc** leaves full screen.
- **Next:** right arrow or space. **Back:** left arrow. A clicker's Page Down / Page Up also
  work. Mouse clicks do not change the slide.
- Some slides reveal in steps; the script marks each key press as **[press]**.
- **P** opens the presenter window (notes, a 10-minute timer and each speaker's time budget).
  Use it with an extended display, not a mirrored one.
- **B** blanks the screen. `index.html#7` opens slide 7 directly.
- Without internet the fonts fall back to Bahnschrift, so open the deck once online on the
  presenting laptop to cache them.

## Time budget

| Speaker | Slides | Time |
|---|---|---|
| Mujuni Innocent | 1–6 | 2:45 |
| Martha Namuyimbwa | 7–9 | 2:05 |
| Ochen Felix | 10–11 | 1:35 |
| Nakimuli Jollyne Flavia | 12–13 | 1:30 |
| Tamara Alice Nazziwa | 14–16 | 1:45 |
| Group 15 | 17 | 0:20 |

## Mujuni Innocent, 24/U/07155/PS (2:45)

*Overview, integration, storage and synchronisation*

### Slide 1: Title

Good morning. We are Group 15, and this is our Smart Control and Monitoring System for CoCIS.

I am Mujuni Innocent, registration number 24/U/07155/PS. Each of us will present the part we built, in the order on the screen.

### Slide 2: Problem

CoCIS has two problems: valuable equipment, so it must control who comes and goes and know who is inside; and high electricity bills, partly from lights left on in empty lecture rooms.

Management asked for a pilot on the main entrance, with separate entry and exit doors, and two lecture rooms.

### Slide 3: System overview

Our system is built on the ESP32-S3 with ESP-IDF and FreeRTOS, and it has three terminals.

First, the main entrance hub. It uses two RFID readers and servo door locks to verify users at entry and exit, track who is inside, and enforce opening hours, anti-passback and an emergency break-glass override.

Second and third, lecture rooms 1 and 2. They use motion and light sensors to switch and dim their lamps automatically, use daylight, and report the energy saved to the hub.

### Slide 4: Architecture and start-up order

My personal role was system integration, resource synchronisation, NVS persistence and SD card storage.

First, in main.c, I designed the boot sequence along the bottom: time, storage and shared state start in the right order before the FreeRTOS tasks of the three terminals.

The hub is the single authority: the readers, the button and the rooms only send it messages.

### Slide 5: Synchronisation

Second, in bus.c, I built the message bus on FreeRTOS queues. Each message carries its sender and a sequence number, and a per-sender mutex keeps the numbers in order, so the receiver can safely drop duplicates and replays.

That is our answer to requirement 2.7: every shared resource has exactly one owner or one mutex.

Third, in occupancy.c and nvs_store.c, the list of people inside is protected by a mutex and saved to flash, so it survives power cuts.

### Slide 6: Tamper-evident log

Finally, in storage.c, I built a gatekeeper task: the only code that writes the SD card. All events go into daily log files.

Each line's hash includes the previous line's hash, so the lines form a chain. log verify recomputes it.

**[Press]** If someone changes Alice's refused scan to "granted", the hash no longer matches, and log verify points to line 9.

Thank you. I now hand over to Martha.

> *If asked:* without a secret key, someone who rewrites every hash could hide an edit, and cutting off the last lines is not detected. An off-device copy (the dashboard) covers that.

## Martha Namuyimbwa, 24/U/09436/PS (2:05)

*Entrance access control*

### Slide 7: A card tap

I am Martha Namuyimbwa, registration number 24/U/09436/PS. I built the entrance access control.

When Alice taps her blue card at the entry reader: **[press]** the reader task reads the card with my MFRC522 driver and sends a card-scan message to the hub. **[press]** The hub looks her up. **[press]** It gathers the facts. **[press]** access_decide gives the verdict.

**[press]** If granted, she is marked inside and only the entry door opens, for three seconds. **[press]** Every scan, granted or refused, is logged, and the LCD and buzzer give feedback.

### Slide 8: Access rules

The rules are one pure C function in access_policy.c, read top to bottom; the first rule that matches wins.

Unknown or disabled cards are refused. Anti-passback: you cannot enter twice without leaving, so one card cannot let two people in, and the list of who is inside stays true. Lockdown lets only security and admin through. Inside opening hours everyone passes. After hours only privileged roles pass, or ordinary users leaving during an escort window.

These are the four demo cards. Because the function has no hardware calls, we unit test every rule on a PC.

### Slide 9: After hours

The brief says people still inside may not leave after hours. We enforce that, but we made sure nobody is trapped.

Bob is refused at the exit, and an alert tells security someone is waiting. When Carol from security leaves, a 60-second escort window opens, and Bob can leave with her. His exit is still recorded.

In an emergency, the break-glass button holds both doors open and logs an alert; only an admin can clear it.

My files are on the screen: the RFID driver and reader task, the access rules, the scan flow and the doors. Over to Ochen.

## Ochen Felix, 25/U/26621 (1:35)

*Lecture-room lighting*

### Slide 10: Room lighting

I am Ochen Felix, registration number 25/U/26621. I built the lecture-room lighting.

Each room has its own task that runs five times a second and decides its own lamp, so it keeps working even if the hub goes silent.

Motion makes the room occupied and the lamp fades up. A PIR only sees movement, so we wait: with no motion for 30 seconds the room counts as empty. For the last 10 seconds the lamp dims to 30 percent as a warning. **[press]** If someone is still there, they move, and it goes straight back to 100 percent.

### Slide 11: Daylight and energy

Daylight harvesting: at 400 lux or more, the lamp stays off even in an occupied room. It only comes back when the light drops to 150 lux.

The gap between the two thresholds is hysteresis, so a passing cloud does not make the lamp flicker. That is these three lines of code.

For energy, every step adds what the lamp really used. During opening hours it also adds a baseline: a 360-watt lamp bank left on all day. The difference is the energy saved, logged every 10 minutes for each room.

My files are on the screen. Over to Nakimuli.

## Nakimuli Jollyne Flavia, 24/U/0953 (1:30)

*Management console and admin login*

### Slide 12: Two-factor login

I am Nakimuli Jollyne Flavia, registration number 24/U/0953. I built the management interfaces and the authentication.

The brief says only authorised staff may change the access schedule. So every change needs two factors: something you have, the admin's card tapped at a reader, and something you know, a 4-digit PIN typed on the console.

On the right, the schedule change is refused, then accepted after login. Three wrong PINs lock login for 60 seconds, and a session ends after 5 minutes. The PIN is never stored, only a salted SHA-256 hash. Every step is logged.

> *If asked about `tap`:* it is a Wokwi test aid that injects a card from the console; on real hardware it would be removed or require an admin session.

### Slide 13: Serial menu

The serial menu is built on ESP-IDF's console. It is designed around management's questions.

"who" prints the muster list with entry times. "rooms" shows lights and energy. "status", on the right, is the whole system on one screen. "log verify" checks the records.

Anyone can read; only an admin session can change things. I also wrote the LCD and buzzer feedback in ui.c. My files are on the screen. Over to Tamara.

## Tamara Alice Nazziwa, 25/U/03596/PS (1:45)

*Testing and cloud dashboard*

### Slide 14: Testing

I am Tamara Alice Nazziwa, registration number 25/U/03596/PS. I did the testing and the cloud dashboard.

The decision logic has no hardware calls, so it compiles and runs on a PC with all warnings treated as errors: 70 checks, zero failures.

Three Wokwi scenario scripts drive the real firmware in the simulator: they present cards, move the light sliders and type commands, and fail unless the expected log lines appear. All three pass.

Testing found real bugs, which we fixed. The worst was a crash loop: the hardware lamp-fade interrupt kept firing, so we fade the lamp in software instead.

### Slide 15: Dashboard

The dashboard is an optional extra built with Next.js and a Postgres database, deployed on Vercel. It shows who is inside, the rooms, alerts and the access log.

It only accepts event batches signed with HMAC-SHA256 and rejects stale timestamps. Each event is stored once, so sending a batch again changes nothing.

The building never depends on it. Today it is fed by a signed sample sender; connecting the hub's uploader is the next step.

### Slide 16: Summary

To sum up: every requirement in the brief, on the left, is met. On the right are the features we added to make the system more secure, efficient, reliable and useful to management.

## Group 15 (0:20)

*Questions*

### Slide 17: Thank you

Thank you for listening. The Wokwi link runs the whole system in the browser, and the video shows each of our parts. We are happy to take questions.
