# ESP32 Strava Running Shoe Dashboard

A standalone touchscreen dashboard that tracks running-shoe mileage from Strava.

It is designed for the **ESP32-2432S028R**, commonly called the **Cheap Yellow Display (CYD)**. The dashboard shows each pair of shoes with:

- Shoe name
- Total distance in miles
- Replacement-mileage target
- Green / amber / red health bar
- “Soon” and “Replace” warnings
- Tap a shoe to open its detail screen
- Automatic Strava sync every 6 hours
- Local saved mileage when Wi-Fi or Strava is unavailable

> Your Garmin watch uploads runs to Garmin Connect, then to Strava. In Strava, assign each run to the pair of shoes you wore. This dashboard reads the mileage totals from Strava.

---

## Hardware

### Required

| Item | Notes |
|---|---|
| ESP32-2432S028R / CYD | 2.8-inch ESP32 touchscreen board |
| USB data cable | Check whether your exact board has USB-C or micro-USB |
| 5 V USB wall power adapter | 1 A or greater is suitable |
| Wi-Fi network | Needed for Strava updates |

### Optional

| Item | Purpose |
|---|---|
| 3D-printed case, desk stand, or wall enclosure | Mount the dashboard safely |
| Command strips or screws | Wall mounting |
| microSD card | Not needed by the current version, but useful for future backup or image features |

The CYD already includes:

- ESP32 Wi-Fi computer
- 2.8-inch color display
- Touchscreen
- Small speaker
- microSD slot

No separate screen, touchscreen, buzzer, or Wi-Fi module is required.

---

## Screen behavior

The main screen shows up to four shoes at one time.

Each shoe has a health bar based on:

```text
remaining health = replacement mileage - current mileage
