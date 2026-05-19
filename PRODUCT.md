# Product

## Register

product

## Users

**Primary:** Dhruv (age 6). Sitting at a table or on the couch, picking up the device, tapping through games solo. No reading required to navigate — icons and big visuals carry the UI. Short attention span; needs instant visual feedback and clear progress.

**Secondary:** Parent (admin). Occasional: adjusting brightness, checking star count, resetting. Expects a PIN-gated adult layer that doesn't intrude on the child's experience.

## Product Purpose

Offline touchscreen learning toy. Math (counting, skip-counting, addition, multiplication) and Reading (letters, rhymes, word families). Five questions per round, star-only rewards, no timers, no penalties. Goal: make practicing feel like playing, not studying.

Long-term direction: adaptive difficulty (questions get harder as mastery builds).

## Brand Personality

Smart, Focused, Clean. Upscale for a 6-year-old's device: not loud and cluttered, but vivid and deliberate. Every element earns its place. The product treats Dhruv like he's capable — not a baby app, not a dopamine trap.

## Anti-references

- **Generic edu-app aesthetic** (ABCmouse, Khan Kids): pastel clip-art, bubbly cartoon fonts, visual noise everywhere. Avoid.
- **Mobile-game dopamine loop**: coins showering the screen, endless level-up banners, notification badges. Avoid.
- **AI slop / generic LVGL demo**: default LVGL blue theme, rainbow button sets, anything that looks auto-generated. Avoid.

**Level-up / progression** (adaptive difficulty, unlocking harder questions) is a positive direction worth building toward.

## Design Principles

1. **Upscale by restraint.** Fewer elements, more intentional. A single vibrant color pop on a dark ground beats five competing accents.
2. **Touch confidence.** Every tappable surface is large, obvious, and gives immediate feedback. No ambiguity about what's interactive.
3. **Background carries atmosphere.** The launcher should feel like a place, not a menu list. Texture or gradient behind the icons sets the mood; the icons are just the way in.
4. **Progress is visible, not shouted.** Stars accumulate quietly. The count lives in the status bar; it doesn't interrupt gameplay.
5. **Dhruv is the protagonist.** Copy, visuals, and pacing treat him as capable and curious — not as someone who needs to be tricked into learning.

## Accessibility & Inclusion

- Minimum 60px touch targets (hardware constraint: 7" MIPI DSI capacitive touch)
- High contrast: white or gold text on dark backgrounds only
- No timers or countdown pressure — all at child's pace
- No reliance on color alone to convey correctness (text feedback accompanies color wash)
- Fonts: Montserrat 24–48px (embedded), no icon-only affordances without a text label
