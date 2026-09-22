/*
 * CrowPanel Advanced 7-inch ESP32-P4 desktop enclosure
 *
 * Original enclosure geometry derived from Elecrow's official V1.2 Eagle
 * board and 2025-12-29 STEP assembly. Units are millimetres.
 *
 * Export with, for example:
 *   openscad -o front_bezel.stl -D 'part="front"' crowpanel_p4_desk_case.scad
 *
 * Parts: front, rear, stand, coupon, layout, preview
 */

$fn = 48;
part = "preview";

// ---- Authoritative device geometry ---------------------------------------
pcb_w = 176.90;
pcb_h = 104.00;
pcb_t = 1.70;
device_w = 180.00;
device_h = 105.00;
mount_hole_d = 3.20;
mount_x = [3.00, 173.90];
mount_y = [3.00, 101.00];

// STEP active-area bounds relative to the PCB centre. The opening is enlarged
// to the published 155 x 87 mm active area plus 0.5 mm clearance per side.
screen_open_w = 156.00;
screen_open_h = 88.00;
screen_center_x_from_pcb_left = 86.725;
screen_center_y_from_pcb_bottom = 49.84;

// Eagle connector centres. Rear-board X is mirrored so these land on the
// physical LEFT when the display is viewed from the front in landscape.
usb_y = [41.57, 62.47];
power_y = 79.45;
sd_y = 15.20;

// ---- Printable fit parameters --------------------------------------------
wall = 2.60;
rear_wall = 2.80;
xy_clearance = 0.40;       // per side around the 180 x 105 mm envelope
case_w = 185.60;
case_h = 111.00;
case_r = 5.50;
case_margin_x = (case_w - pcb_w) / 2;
case_margin_y = (case_h - pcb_h) / 2;

front_lip = 2.00;
front_depth = 4.80;
pcb_support_z = 8.00;
rear_front_z = 4.80;
rear_inner_z = 20.20;
rear_outer_z = rear_inner_z + rear_wall;

insert_pilot_d = 3.90;    // tune for the exact M3 x 6 x 4.2 insert used
insert_depth = 6.20;
// The lower mounting holes sit close to the active area. A compact 7 mm boss
// preserves the full touch opening while retaining 1.55 mm around the insert.
boss_d = 7.00;
screw_clearance_d = 3.50;
screw_head_d = 6.70;
screw_head_depth = 2.20;

usb_open_h = 12.00;
usb_open_depth_start = 6.20;
usb_open_depth_end = 18.20;
power_open_h = 10.00;
sd_open_h = 8.00;

stand_tilt = 20;          // degrees back from vertical (70 degrees to desk)
stand_w = 174;
stand_d = 76;
stand_base_t = 4.0;
stand_rail_w = 18;
stand_slot = rear_outer_z + 0.80;
stand_seat_y = 16;
stand_seat_z = stand_base_t + rear_outer_z * sin(stand_tilt) + 0.8;

eps = 0.05;

module rounded_rect_2d(w, h, r) {
    rr = min(r, min(w, h) / 2);
    hull() {
        for (x = [rr, w - rr], y = [rr, h - rr])
            translate([x, y]) circle(r = rr);
    }
}

module rounded_box(w, h, d, r) {
    linear_extrude(height = d) rounded_rect_2d(w, h, r);
}

module pcb_mount_positions() {
    for (x = mount_x, y = mount_y)
        translate([case_margin_x + x, case_margin_y + y, 0]) children();
}

module rounded_side_slot_left(yc, h, z0, z1, radius = 2.2) {
    // Capsule-shaped opening through the left wall.
    hull() {
        for (yy = [yc - h / 2 + radius, yc + h / 2 - radius])
            translate([-eps, yy, z0 + radius])
                rotate([0, 90, 0]) cylinder(h = wall + 2 * eps, r = radius);
        for (yy = [yc - h / 2 + radius, yc + h / 2 - radius])
            translate([-eps, yy, z1 - radius])
                rotate([0, 90, 0]) cylinder(h = wall + 2 * eps, r = radius);
    }
}

module rounded_side_slot_right(yc, h, z0, z1, radius = 2.0) {
    hull() {
        for (yy = [yc - h / 2 + radius, yc + h / 2 - radius])
            translate([case_w - wall - eps, yy, z0 + radius])
                rotate([0, 90, 0]) cylinder(h = wall + 2 * eps, r = radius);
        for (yy = [yc - h / 2 + radius, yc + h / 2 - radius])
            translate([case_w - wall - eps, yy, z1 - radius])
                rotate([0, 90, 0]) cylinder(h = wall + 2 * eps, r = radius);
    }
}

module front_bezel() {
    difference() {
        union() {
            difference() {
                rounded_box(case_w, case_h, front_depth, case_r);

                // Touch opening. Its STEP-derived offset keeps the entire
                // active area visible rather than assuming a centred panel.
                translate([
                    case_margin_x + screen_center_x_from_pcb_left - screen_open_w / 2,
                    case_margin_y + screen_center_y_from_pcb_bottom - screen_open_h / 2,
                    -eps
                ])
                    rounded_box(screen_open_w, screen_open_h,
                                front_depth + 2 * eps, 2.2);

                // Relief behind the 2 mm front lip. This avoids loading the
                // glass and leaves room for a 0.5-1 mm foam/TPU gasket.
                translate([(case_w - (device_w + 2 * xy_clearance)) / 2,
                           (case_h - (device_h + 2 * xy_clearance)) / 2,
                           front_lip])
                    rounded_box(device_w + 2 * xy_clearance,
                                device_h + 2 * xy_clearance,
                                front_depth - front_lip + eps, 3.6);
            }

            // Panel supports. M3 heat-set inserts install from the rear.
            pcb_mount_positions()
                cylinder(h = pcb_support_z, d = boss_d);
        }

        pcb_mount_positions()
            translate([0, 0, pcb_support_z - insert_depth])
                cylinder(h = insert_depth + eps, d = insert_pilot_d);
    }
}

module acoustic_slots() {
    // Two understated rear banks: enough for the on-device audio without
    // turning the requested closed back into an open development enclosure.
    for (bank_x = [case_w * 0.27, case_w * 0.73])
        for (i = [-2 : 2])
            translate([bank_x - 12, case_h * 0.28 + i * 3.2,
                       rear_inner_z - eps])
                rounded_box(24, 1.6, rear_wall + 2 * eps, 0.8);
}

module rear_shell() {
    difference() {
        translate([0, 0, rear_front_z])
            rounded_box(case_w, case_h, rear_outer_z - rear_front_z, case_r);

        // Main cavity: 0.4 mm per-side clearance around the published device
        // envelope and at least 1.5 mm behind the deepest STEP component.
        translate([(case_w - (device_w + 2 * xy_clearance)) / 2,
                   (case_h - (device_h + 2 * xy_clearance)) / 2,
                   rear_front_z - eps])
            rounded_box(device_w + 2 * xy_clearance,
                        device_h + 2 * xy_clearance,
                        rear_inner_z - rear_front_z + eps, 3.8);

        // Two independent USB-C openings on the front-view left edge.
        for (yc = usb_y)
            rounded_side_slot_left(case_margin_y + yc, usb_open_h,
                                   usb_open_depth_start, usb_open_depth_end);

        // Recessed power slider access above the USB pair.
        rounded_side_slot_left(case_margin_y + power_y, power_open_h,
                               7.0, 16.5, 1.8);

        // microSD remains accessible on the opposite edge.
        rounded_side_slot_right(case_margin_y + sd_y, sd_open_h,
                                7.0, 16.5, 1.6);

        acoustic_slots();

        // Four rear-access screws clamp shell + PCB into the front inserts.
        pcb_mount_positions() {
            translate([0, 0, rear_inner_z - eps])
                cylinder(h = rear_wall + 2 * eps, d = screw_clearance_d);
            translate([0, 0, rear_outer_z - screw_head_depth])
                cylinder(h = screw_head_depth + eps, d = screw_head_d);
        }
    }
}

module stand_base() {
    translate([(case_w - stand_w) / 2, -stand_d * 0.32, 0])
        rounded_box(stand_w, stand_d, stand_base_t, 7);
}

module stand_rail(x0) {
    // Each rail is a strong printable cradle. The angled cavity holds the
    // lower 39 mm of the closed case at 20 degrees back from vertical.
    difference() {
        translate([x0, -10, stand_base_t - eps])
            rounded_box(stand_rail_w, 61, 35, 4);

        translate([x0 - 1, stand_seat_y, stand_seat_z])
            rotate([-stand_tilt, 0, 0])
                cube([stand_rail_w + 2, stand_slot, 52]);

        // Rear material relief; leaves a 4 mm structural web.
        translate([x0 + 3.5, 38, stand_base_t + 8])
            rotate([-stand_tilt, 0, 0])
                cube([stand_rail_w - 7, 20, 32]);
    }
}

module angled_desk_stand() {
    union() {
        stand_base();
        stand_rail((case_w - stand_w) / 2 + 20);
        stand_rail(case_w - (case_w - stand_w) / 2 - 20 - stand_rail_w);

        // Low front cable-management bridge. The left side remains open so
        // USB-C plugs are not forced through a narrow tunnel.
        translate([(case_w - stand_w) / 2 + 48, -22, stand_base_t - eps])
            rounded_box(stand_w - 72, 8, 6, 2);
    }
}

module fit_coupon() {
    // Print first: validates wall/USB aperture and heat-insert sizing using
    // only a few grams of filament.
    difference() {
        union() {
            rounded_box(34, 52, 14, 3);
            translate([25, 10, 0]) cylinder(h = 14, d = boss_d);
        }
        translate([-eps, 8, 3]) cube([wall + 2 * eps, 12, 8]);
        translate([-eps, 29, 3]) cube([wall + 2 * eps, 12, 8]);
        translate([25, 10, 14 - insert_depth])
            cylinder(h = insert_depth + eps, d = insert_pilot_d);
        translate([8, 44, -eps])
            linear_extrude(height = 1.0) text("L USB", size = 4,
                                               font = "Arial:style=Bold");
    }
}

module print_layout() {
    front_bezel();
    translate([case_w + 10, 0, rear_outer_z]) rotate([180, 0, 0]) rear_shell();
    translate([0, case_h + 15, 0]) angled_desk_stand();
    translate([case_w + 18, case_h + 18, 0]) fit_coupon();
}

module preview_assembly() {
    color([0.16, 0.16, 0.17]) angled_desk_stand();

    // Seat the complete case in the exact transform used to cut the cradle.
    translate([0, stand_seat_y, stand_seat_z])
        rotate([-stand_tilt, 0, 0])
            rotate([90, 0, 0])
                mirror([0, 0, 1]) {
                    color([0.035, 0.035, 0.04]) front_bezel();
                    color([0.09, 0.09, 0.10]) rear_shell();
                    color([0.01, 0.01, 0.012])
                        translate([
                            case_margin_x + screen_center_x_from_pcb_left - screen_open_w / 2,
                            case_margin_y + screen_center_y_from_pcb_bottom - screen_open_h / 2,
                            -0.12
                        ])
                            rounded_box(screen_open_w, screen_open_h, 0.10, 2.2);
                }
}

if (part == "front") front_bezel();
else if (part == "rear") rear_shell();
else if (part == "stand") angled_desk_stand();
else if (part == "coupon") fit_coupon();
else if (part == "layout") print_layout();
else if (part == "preview_front") rotate([0, 0, 180]) preview_assembly();
else preview_assembly();
