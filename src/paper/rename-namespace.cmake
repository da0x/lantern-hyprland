# SPDX-License-Identifier: GPL-3.0-or-later
file(READ ${HEADER} text)
string(REPLACE "*namespace" "*name_space" text "${text}")
string(REPLACE ", namespace)" ", name_space)" text "${text}")
file(WRITE ${HEADER} "${text}")
