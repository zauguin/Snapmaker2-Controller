/**
 * Marlin 3D Printer Firmware
 * Copyright (c) 2020 MarlinFirmware [https://github.com/MarlinFirmware/Marlin]
 *
 * Based on Sprinter and grbl.
 * Copyright (c) 2011 Camiel Gubbels / Erik van der Zalm
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */
#pragma once

/**
 * feature/spindle_laser.h
 * Support for Laser Power or Spindle Power & Direction
 */

#include "../inc/MarlinConfig.h"

#include "spindle_laser_types.h"
#include "module/toolhead_cnc.h"
#include "module/toolhead_laser.h"

#if ENABLED(LASER_POWER_INLINE)
  #include "../module/planner.h"
#endif

#ifndef SPEED_POWER_INTERCEPT
  #define SPEED_POWER_INTERCEPT 0
#endif
#define SPEED_POWER_FLOOR TERN(CUTTER_POWER_RELATIVE, SPEED_POWER_MIN, 0)

// Ensure:
static_assert(CUTTER_UNIT_IS(PERCENT), "Unsupported unit");

class SpindleLaser {
public:
  static constexpr float min_pct = 0, max_pct = 100;
  static uint8_t pct_to_ocr(const float pct) {
    if (laser.IsOnline())
      return pct <= 1 ? uint8_t(round(pct * 20)) : 20 + uint8_t(round((pct-1) * 235 / 99));
    return uint8_t(round(pct));
  }

  // Convert a configured value (cpower)(ie SPEED_POWER_STARTUP) to unit power (upwr, upower),
  // which can be PWM, Percent, or RPM (rel/abs).
  // In the Snapmaker case we always use Percent values anyway.
  static constexpr inline cutter_power_t cpwr_to_upwr(const cutter_cpower_t cpwr) { return cpwr; } // STARTUP power to Unit power

  static bool isReady;                    // Ready to apply power setting from the UI to OCR
  static uint8_t power, power_limit;

  static cutter_power_t menuPower,        // Power as set via LCD menu in PWM, Percentage or RPM
                        unitPower;        // Power as displayed status in PWM, Percentage or RPM

  static void init() {
    power_limit = upower_to_ocr(SPEED_POWER_MAX);
  }

  // Modifying this function should update everywhere
  static inline bool enabled(const cutter_power_t opwr) { return opwr > 0; }
  static inline bool enabled() { return enabled(power); }

  static void apply_power(const uint8_t inpow);

  FORCE_INLINE static void refresh() { apply_power(power); }
  FORCE_INLINE static void set_power(const uint8_t upwr) { power = upwr; refresh(); }

  FORCE_INLINE static void set_power_limit(cutter_power_t new_limit = SPEED_POWER_SAFE_LIMIT) {
    power_limit = upower_to_ocr(new_limit);
    if (enabled()) set_ocr(power);
  }
  FORCE_INLINE static void reset_power_limit() { set_power_limit(SPEED_POWER_MAX); }

  #if ENABLED(SPINDLE_LASER_PWM)

    static void set_ocr(uint8_t ocr);
    static inline void set_ocr_power(const uint8_t ocr) { power = ocr; set_ocr(ocr); }
    static void ocr_off();
    // Used to update output for power->OCR translation
    static inline uint8_t upower_to_ocr(const cutter_power_t upwr) {
      return (
        #if CUTTER_UNIT_IS(PWM255)
          uint8_t(upwr)
        #elif CUTTER_UNIT_IS(PERCENT)
          pct_to_ocr(upwr)
        #else
          uint8_t(pct_to_ocr(cpwr_to_pct(upwr)))
        #endif
      );
    }

    // Correct power to configured range
    static inline cutter_power_t power_to_range(const cutter_power_t pwr) {
      return power_to_range(pwr, (
        #if CUTTER_UNIT_IS(PWM255)
          0
        #elif CUTTER_UNIT_IS(PERCENT)
          1
        #elif CUTTER_UNIT_IS(RPM)
          2
        #else
          #error "???"
        #endif
      ));
    }
    static inline cutter_power_t power_to_range(const cutter_power_t pwr, const uint8_t pwrUnit) {
      if (pwr <= 0) return 0;
      auto toolhead = ModuleBase::toolhead();
      if (pwrUnit == 0 && toolhead == MODULE_TOOLHEAD_LASER && pwr >= 255) return 255;
      if ((pwrUnit == 1 || (pwrUnit == 0 && toolhead == MODULE_TOOLHEAD_CNC)) && pwr >= 100) return 100;
      return pwr;
    }

  #endif // SPINDLE_LASER_PWM

  static inline void set_enabled(const bool enable) {
    set_power(enable ? TERN(SPINDLE_LASER_PWM, (power ?: (unitPower ? upower_to_ocr(cpwr_to_upwr(SPEED_POWER_STARTUP)) : 0)), 255) : 0);
  }

  // Wait for spindle to spin up or spin down
  static inline void power_delay(const bool on) {
    if (cnc.IsOnline())
      safe_delay(on ? SPINDLE_LASER_POWERUP_DELAY : SPINDLE_LASER_POWERDOWN_DELAY);
  }

  #if ENABLED(SPINDLE_CHANGE_DIR)
    static void set_direction(const bool reverse);
  #else
    static inline void set_direction(const bool) {}
  #endif

  static inline void disable() { isReady = false; set_enabled(false); }

  #if 1 || HAS_LCD_MENU

    static inline void enable_with_dir(const bool reverse) {
      isReady = true;
      const uint8_t ocr = TERN(SPINDLE_LASER_PWM, upower_to_ocr(menuPower), 255);
      if (menuPower)
        power = ocr;
      else
        menuPower = cpwr_to_upwr(SPEED_POWER_STARTUP);
      unitPower = menuPower;
      set_direction(reverse);
      set_enabled(true);
    }
    FORCE_INLINE static void enable_forward() { enable_with_dir(false); }
    FORCE_INLINE static void enable_reverse() { enable_with_dir(true); }

    #if ENABLED(SPINDLE_LASER_PWM)
      static inline void update_from_mpower() {
        if (isReady) power = upower_to_ocr(menuPower);
        unitPower = menuPower;
      }
    #endif

  #endif

  #if ENABLED(LASER_POWER_INLINE)
    /**
     * Inline power adds extra fields to the planner block
     * to handle laser power and scale to movement speed.
     */

    // Force disengage planner power control
    static inline void inline_disable() {
      isReady = false;
      unitPower = 0;
      planner.laser_inline.status.isPlanned = false;
      planner.laser_inline.status.isEnabled = false;
      planner.laser_inline.power = 0;
    }

    // Inline modes of all other functions; all enable planner inline power control
    static inline void set_inline_enabled(const bool enable) {
      if (enable)
        inline_power(cpwr_to_upwr(SPEED_POWER_STARTUP));
      else {
        isReady = false;
        unitPower = menuPower = 0;
        planner.laser_inline.status.isPlanned = false;
        TERN(SPINDLE_LASER_PWM, inline_ocr_power, inline_power)(0);
      }
    }

    // Set the power for subsequent movement blocks
    static void inline_power(const cutter_power_t upwr) {
      unitPower = menuPower = upwr;
      #if ENABLED(SPINDLE_LASER_PWM)
        #if ENABLED(SPEED_POWER_RELATIVE) && !CUTTER_UNIT_IS(RPM) // relative mode does not turn laser off at 0, except for RPM
          planner.laser_inline.status.isEnabled = true;
          planner.laser_inline.power = upower_to_ocr(upwr);
          isReady = true;
        #else
          inline_ocr_power(upower_to_ocr(upwr));
        #endif
      #else
        planner.laser_inline.status.isEnabled = enabled(upwr);
        planner.laser_inline.power = upwr;
        isReady = enabled(upwr);
      #endif
    }

    static inline void inline_direction(const bool) { /* never */ }

    #if ENABLED(SPINDLE_LASER_PWM)
      static inline void inline_ocr_power(const uint8_t ocrpwr) {
        isReady = ocrpwr > 0;
        planner.laser_inline.status.isEnabled = ocrpwr > 0;
        planner.laser_inline.power = ocrpwr;
      }
    #endif
  #endif  // LASER_POWER_INLINE

  static inline void kill() {
    TERN_(LASER_POWER_INLINE, inline_disable());
    disable();
  }
};

extern SpindleLaser cutter;
