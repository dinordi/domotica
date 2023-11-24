# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
set(BOARD nrf52833dk_nrf52833)
# set(BOARD nrf52840dongle_nrf52840)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(domotica)

FILE(GLOB app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})
