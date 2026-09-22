# CrowPanel Advanced 7-inch ESP32-P4 desk case

An original, parametric enclosure for the Elecrow **CrowPanel Advanced 7-inch
ESP32-P4 HMI** (`DHE04107D` / Amazon `B0G34WGWJR`). It holds the screen in
landscape, presents both USB-C ports on the **left when viewed from the front**,
closes the electronics, and docks into a wide 20-degree-back desktop stand.

This model was built from Elecrow's official mechanical data, not from listing
photos or a similarly named older ESP32-S3 panel:

- [Official 2025-12-29 STEP assembly](https://github.com/Elecrow-RD/CrowPanel-Advanced-7inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen/blob/master/3D%20file/ESP32-P4-7_0-inch-20251229.stp)
- [Official V1.2 Eagle board](https://github.com/Elecrow-RD/CrowPanel-Advanced-7inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen/blob/master/Eagle_SCH%26PCB/1.2/ESP32-P4%20Display%207.0%20inch%20V1.2.brd)
- [Elecrow product specification](https://www.elecrow.com/crowpanel-advanced-7inch-esp32-p4-hmi-ai-display-1024x600-ips-touch-screen-with-wifi-6-compatible-with-arduino-lvgl-micropython.html)

The exact-board [MakerWorld model 3146102](https://makerworld.com/en/models/3146102)
was reviewed for assembly ideas only. Its geometry was not used or repackaged;
this case is independently constructed from Elecrow's CAD because that model's
exclusive licence does not permit copying and its open rear does not match this
project's closed-back requirement.

## Verified device dimensions

| Feature | Dimension |
|---|---:|
| Published assembly envelope | 180 x 105 mm |
| PCB outline | 176.90 x 104.00 mm, R3 corners |
| Active screen | 155 x 87 mm published; 154.21 x 85.92 mm in STEP |
| Mounting holes | 4 x 3.2 mm |
| Hole spacing | 170.90 x 98.00 mm |
| USB-C vertical centres | 41.57 and 62.47 mm from PCB bottom |
| USB-C centre spacing | 20.90 mm |

The official STEP gives approximately 15.8 mm from the front display surface to
the deepest normal rear component. The case provides a 20.2 mm internal rear
plane plus a 2.8 mm back wall. Flex tails and the optional camera should be
routed flat inside the free volume; the camera is not given an external opening.

## Parts and hardware

Export these from [`crowpanel_p4_desk_case.scad`](crowpanel_p4_desk_case.scad):

- `front` — thin front bezel with four panel supports
- `rear` — closed rear shell with USB-C, power, microSD, and acoustic openings
- `stand` — wide two-rail 70-degree desk cradle
- `coupon` — quick USB-aperture and heat-insert tolerance test

Hardware:

- 4 x M3 x 6 mm x 4.2 mm OD heat-set inserts
- 4 x M3 x 20 mm socket-head screws
- 0.5-1.0 mm self-adhesive foam or printed TPU gasket around the glass perimeter
- Optional thin rubber feet for the stand

Never clamp bare printed plastic hard against the touch glass. Use the gasket
and tighten the four screws evenly only until the assembly cannot move.

## Export

OpenSCAD 2021.01 or newer:

```sh
openscad -o crowpanel_p4_front_bezel.stl \
  -D 'part="front"' crowpanel_p4_desk_case.scad
openscad -o crowpanel_p4_closed_rear.stl \
  -D 'part="rear"' crowpanel_p4_desk_case.scad
openscad -o crowpanel_p4_angled_stand.stl \
  -D 'part="stand"' crowpanel_p4_desk_case.scad
openscad -o crowpanel_p4_fit_coupon.stl \
  -D 'part="coupon"' crowpanel_p4_desk_case.scad
```

Print the coupon first. Heat-set insert fit varies more with printer/material
than the nominal insert diameter suggests; change `insert_pilot_d` if needed.

## Print guidance

- PETG preferred; PLA is acceptable away from sun/heat
- 0.20 mm layer height, 3-4 walls, 20% gyroid infill
- Front: glass-facing side flat on the bed
- Rear: closed back flat on the bed
- Stand: base flat on the bed
- Coupon: engraved face up
- No supports should be required on a well-tuned printer

The enclosure footprint is 185.6 x 111 mm and fits a 256 mm square build plate.
The stand is 174 x 76 mm. The 0.4 mm-per-side panel clearance is intentionally
conservative; adjust `xy_clearance` after the coupon and a dry fit if your
printer consistently runs tight or loose.

## Bambu Studio validation

The committed STL files were imported through Bambu Studio 2.7.1.62's own
command-line parser. All four returned exit code 0, `manifold = yes`, and
`number_of_parts = 1`:

| STL | Bambu-reported size (mm) | Facets |
|---|---:|---:|
| Front bezel | 185.6 x 111.0 x 8.0 | 2,160 |
| Closed rear | 185.6 x 111.0 x 18.2 | 4,852 |
| Angled stand | 174.0 x 76.0 x 38.95 | 892 |
| Fit coupon | 34.0 x 52.0 x 14.0 | 2,368 |

They were also independently checked for watertightness and consistent winding
with Trimesh. These checks establish that the files are valid slicer inputs;
they do not replace the recommended physical coupon and dry-fit test.

## Assembly

1. Print and check the coupon with the intended USB cable and heat-set insert.
2. Install the four inserts from the rear of the front bezel supports.
3. Apply a thin gasket only on the opaque perimeter outside the active/touch area.
4. Place the display face-down into the bezel without forcing the glass.
5. Route the flex tails flat and align all four PCB holes with the supports.
6. Fit the closed rear and start all four M3 screws before tightening diagonally.
7. Dock the lower edge into both stand rails, with the USB-C side on the left.

Before a long unattended print, compare the generated preview and port locations
against the physical panel. Elecrow has shipped multiple CrowPanel families with
different mechanics; this design is specifically for the **Advanced ESP32-P4
7-inch V1.2** represented by the linked CAD.
