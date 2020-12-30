/*
 * Copyright (C) 2020 Marcel Krüger
 *
 * This file is part of Marcel's fork of Snapmaker2-Controller
 * (see https://github.com/zauguin/Snapmaker2-Controller)
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "module/toolhead_laser.h"
#include <src/gcode/gcode.h>
#include <src/module/planner.h>

void GcodeSuite::M7() {
  planner.synchronize();
  if (MODULE_TOOLHEAD_LASER == ModuleBase::toolhead())
    laser.SetFan(true);
}

void GcodeSuite::M9() {
  planner.synchronize();
  if (MODULE_TOOLHEAD_LASER == ModuleBase::toolhead())
    laser.SetFan(false);
}
