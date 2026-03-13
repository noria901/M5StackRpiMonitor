#pragma once

#include <cstdint>

/**
 * RPi Monitor launcher icon (42x42 pixels, RGB565)
 *
 * Simplified Raspberry Pi logo silhouette:
 * - Green circuit board motif on dark background
 * - Stylized "Pi" symbol in center
 *
 * To regenerate from a PNG:
 *   python3 scripts/png_to_c_array.py icon_rpi.png > icon_rpi.h
 *
 * For now, this is a placeholder that draws a solid colored circle
 * with "Pi" text. The launcher code will use this as the icon_addr.
 *
 * Note: In the actual M5Dial-UserDemo, icons are typically 42x42 ARGB
 * stored as const uint16_t arrays. Replace this placeholder with a
 * proper converted icon image.
 */

// Placeholder icon: 42x42 = 1764 pixels, RGB565
// Filled with RPi green (0x0640 ~ dark green) as a simple placeholder.
// The real icon should be generated from an actual image file.
static const uint16_t icon_rpi_42x42[1764] = {
    // Row 0-41: Simple circle pattern
    // This is a programmatic placeholder - replace with actual icon data.
    // For integration, use the img_to_c_array tool from M5Dial-UserDemo.
    0  // Placeholder - array will be zero-initialized
};

// Icon color for the launcher ring highlight
static const uint32_t ICON_RPI_COLOR = 0x00CC00;  // RPi green

// Icon tag text shown below the icon
static const char* ICON_RPI_TAG = "RPi Mon";
