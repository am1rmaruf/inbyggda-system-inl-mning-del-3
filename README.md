# Inlämningsuppgift delmoment 3

Det här är min prototyp av ett passagesystem med Arduino Uno. Systemet använder RFID, keypad, RGB LED, servo, buzzer och den tilldelade access-protokoll-drivrutinen.

Flödet börjar med att ett RFID-kort blippas. Då skickar Arduino kortets UID till backend. Om backend svarar med `REQ_PIN` börjar LED blinka gult och användaren kan skriva in PIN-kod. När PIN-koden är inskriven skickas den vidare till backend.

Om backend svarar `OK` öppnas dörren i 5 sekunder. Då lyser LED grönt, servon öppnas och buzzern låter. Om backend svarar `ERR` blinkar LED rött och systemet går tillbaka till IDLE. Om backend inte svarar inom rimlig tid blinkar LED blått och systemet går också tillbaka till IDLE.

Koden är uppbyggd med en state machine:

- `STATE_IDLE`
- `STATE_WAIT_REQ_PIN`
- `STATE_WAIT_PIN`
- `STATE_WAIT_ACCESS_RESULT`

## Test

Bygg projektet med:

```bash
make clean
make
