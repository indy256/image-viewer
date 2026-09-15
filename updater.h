// Copyright (C) 2026 indy256
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QString>

class QWidget;

// True means the replacement helper is ready and the viewer must exit.
bool updateToLatestVersion(QWidget *parent, const QString &imagePath);
