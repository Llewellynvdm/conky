/*
 *
 * Conky, a system monitor, based on torsmo
 *
 * Any original torsmo code is licensed under the BSD license
 *
 * All code written since the fork of torsmo is licensed under the GPL
 *
 * Please see COPYING for details
 *
 * Copyright (c) 2005-2024 Brenden Matthews, Philip Kovacs, et. al.
 *	(see AUTHORS)
 * All rights reserved.
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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "catch2/catch.hpp"

#include <data/os/linux.h>

#include <sstream>
#include <vector>

TEST_CASE("get_entropy_avail returns 0", "[get_entropy_avail]") {
  unsigned int unused = 0;
  REQUIRE(get_entropy_avail(&unused) == 0);
}

TEST_CASE("cpu_present_slot maps cpu numbers to slots", "[cpu]") {
  SECTION("contiguous present set") {
    std::vector<int> present{0, 1, 2, 3};
    REQUIRE(cpu_present_slot(present, 0) == 1);
    REQUIRE(cpu_present_slot(present, 3) == 4);
    REQUIRE(cpu_present_slot(present, 4) == -1);
  }

  SECTION("sparse present set (e.g. /sys reports \"0,3-7\")") {
    std::vector<int> present{0, 3, 4, 5, 6, 7};
    REQUIRE(cpu_present_slot(present, 0) == 1);
    REQUIRE(cpu_present_slot(present, 3) == 2);
    REQUIRE(cpu_present_slot(present, 7) == 6);
    // gaps are not present
    REQUIRE(cpu_present_slot(present, 1) == -1);
    REQUIRE(cpu_present_slot(present, 2) == -1);
    REQUIRE(cpu_present_slot(present, 8) == -1);
  }

  SECTION("empty present set") {
    std::vector<int> present{};
    REQUIRE(cpu_present_slot(present, 0) == -1);
  }
}

TEST_CASE("get_kv_field reads KEY=VALUE entries", "[distribution]") {
  SECTION("quoted value (Fedora os-release NAME)") {
    std::istringstream in(
        "NAME=\"Fedora Linux\"\n"
        "VERSION=\"43 (Workstation Edition)\"\n"
        "ID=fedora\n"
        "PRETTY_NAME=\"Fedora Linux 43 (Workstation Edition)\"\n");
    REQUIRE(get_kv_field(in, "NAME") == "Fedora Linux");
  }

  SECTION("unquoted value") {
    std::istringstream in("ID=gentoo\nNAME=Gentoo\n");
    REQUIRE(get_kv_field(in, "NAME") == "Gentoo");
  }

  SECTION("matches the whole key, not a suffix like PRETTY_NAME") {
    std::istringstream in(
        "PRETTY_NAME=\"Should be ignored\"\n"
        "CPE_NAME=\"cpe:/o:fedoraproject:fedora:43\"\n"
        "NAME=\"Arch Linux\"\n");
    REQUIRE(get_kv_field(in, "NAME") == "Arch Linux");
  }

  SECTION("reads arbitrary fields") {
    std::istringstream in("ID=fedora\nVERSION_ID=43\n");
    REQUIRE(get_kv_field(in, "ID") == "fedora");
  }

  SECTION("empty value yields no result") {
    std::istringstream in("NAME=\"\"\n");
    REQUIRE(get_kv_field(in, "NAME") == std::nullopt);
  }

  SECTION("missing key yields no result") {
    std::istringstream in("ID=void\nVERSION_ID=20240101\n");
    REQUIRE(get_kv_field(in, "NAME") == std::nullopt);
  }
}

TEST_CASE("parse_distribution_from_proc_version", "[distribution]") {
  SECTION("extracts distribution before version number") {
    REQUIRE(parse_distribution_from_proc_version(
                "Linux version 5.10.0-21-amd64 "
                "(debian-kernel@lists.debian.org) "
                "(gcc-10 (Debian 10.2.1-6) 10.2.1) #1 SMP Debian") == "Debian");
  }

  SECTION("no parenthesised uppercase token yields no result") {
    REQUIRE(parse_distribution_from_proc_version(
                "Linux version 6.0.0 (builduser@host) #1") == std::nullopt);
  }

  SECTION("empty input yields no result") {
    REQUIRE(parse_distribution_from_proc_version("") == std::nullopt);
  }
}
